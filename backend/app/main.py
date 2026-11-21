from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.exceptions import StarletteHTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from app.api.v1.router import api_v1_router
from app.core.config import settings
from app.core.deps import get_collector
from app.core.errors import Result, ResultEnvelopeMiddleware
from app.core.logging import setup_logging
from app.db.pool import DatabasePool
from app.services.collector_orchestrator import CollectorOrchestrator
from app.services.event_dispatcher import EventDispatcher
from app.services.ingest_service import SamplePump
from app.ws.hub import WsHub

setup_logging()

db_pool = DatabasePool()
ws_hub = WsHub()
collector = None
pump: SamplePump | None = None
event_dispatcher: EventDispatcher | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global collector, pump, event_dispatcher
    # start
    await db_pool.init()
    collector = get_collector()
    collector.init()
    event_dispatcher = EventDispatcher(db_pool, collector, ws_hub)
    event_dispatcher.start()
    orchestrator = CollectorOrchestrator(db_pool, collector)
    await orchestrator.bootstrap()
    pump = SamplePump(db_pool, collector, ws_hub)
    pump.start()
    yield
    # stop
    if pump:
        pump.stop()
    if event_dispatcher:
        event_dispatcher.stop()
    if collector:
        collector.shutdown()
    await db_pool.close()


def create_app() -> FastAPI:
    app = FastAPI(
        title="IBMCP",
        version="0.1.0",
        lifespan=lifespan,
        openapi_url="/api/v1/openapi.json",
        docs_url="/api/v1/docs",
        redoc_url="/api/v1/redoc",
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=settings.cors_origins,
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.add_middleware(ResultEnvelopeMiddleware)
    app.include_router(api_v1_router, prefix="/api/v1")

    @app.exception_handler(StarletteHTTPException)
    async def http_exception_handler(request: Request, exc: StarletteHTTPException):
        """鉴权 401 / 路由 404 也走统一包膜（HTTP 恒 200，code 表达业务结果）。"""
        return JSONResponse(
            status_code=200,
            content=Result(code=exc.status_code, message=str(exc.detail), data=None).model_dump(),
        )

    return app


app = create_app()
