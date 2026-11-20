from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api.v1.router import api_v1_router
from app.core.config import settings
from app.core.deps import get_collector
from app.core.errors import ResultEnvelopeMiddleware
from app.core.logging import setup_logging
from app.db.pool import DatabasePool
from app.services.collector_orchestrator import CollectorOrchestrator
from app.services.ingest_service import SamplePump
from app.ws.hub import WsHub

setup_logging()

db_pool = DatabasePool()
ws_hub = WsHub()
collector = None
pump: SamplePump | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global collector, pump
    # start
    await db_pool.init()
    collector = get_collector()
    collector.init()
    orchestrator = CollectorOrchestrator(db_pool, collector)
    await orchestrator.bootstrap()
    pump = SamplePump(db_pool, collector, ws_hub)
    pump.start()
    yield
    # stop
    if pump:
        pump.stop()
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
    return app


app = create_app()
