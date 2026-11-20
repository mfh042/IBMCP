FROM openeuler/openeuler:24.03-lts
RUN dnf install -y python3.11 python3.11-pip python3.11-devel gcc-c++ cmake make \
    postgresql-devel redis && dnf clean all
WORKDIR /opt/ibmcp
COPY backend/ ./backend/
COPY python/ ./python/
COPY collector-cpp/ ./collector-cpp/
COPY pyproject.toml ./
RUN python3.11 -m pip install --upgrade pip pybind11 \
 && cmake -S collector-cpp -B collector-cpp/build \
      -DCMAKE_BUILD_TYPE=Release \
      -Dpybind11_DIR="$(python3.11 -c 'import pybind11; print(pybind11.get_cmake_dir())')" \
      -DPython_EXECUTABLE="$(command -v python3.11)" \
 && cmake --build collector-cpp/build -j"$(nproc)" \
 && cmake --install collector-cpp/build --prefix python \
 && python3.11 -m pip install .
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]