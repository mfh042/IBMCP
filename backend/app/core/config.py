from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    app_name: str = "IBMCP"
    app_env: str = "development"
    app_host: str = "0.0.0.0"
    app_port: int = 8000
    app_secret_key: str = "dev-secret-change-me"
    access_token_expire_minutes: int = 1440

    pg_host: str = "127.0.0.1"
    pg_port: int = 5432
    pg_user: str = "ibmcp"
    pg_password: str = "ibmcp"
    pg_database: str = "ibmcp"
    pg_pool_min: int = 5
    pg_pool_max: int = 20

    redis_url: str = "redis://127.0.0.1:6379/0"

    cors_origins: list[str] = Field(default_factory=lambda: ["http://localhost:5173"])

    collector_backend: str = Field(default="stub", description="stub | native")

    @property
    def pg_dsn(self) -> str:
        return (
            f"postgresql://{self.pg_user}:{self.pg_password}"
            f"@{self.pg_host}:{self.pg_port}/{self.pg_database}"
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()


settings = get_settings()
