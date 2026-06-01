# Recordlab_host

C++ Host infrastructure for the RecordLab refactor MVP.

Quick start:

```bash
git clone https://github.com/renhaoyu-xreal/Recordlab_host.git
cd Recordlab_host/host_scripts
./install_dependencies.sh
./start_recordlab.sh
```

The installer clones these repositories into `third_party/`:

```text
third_party/echo_message_system
third_party/Recordlab_nodes
```

It also creates `.venv-py310`, installs the Python packages with Python 3.10,
installs `third_party/xreal_glasses/xreal_glasses-0.4.3-py3-none-any.whl`, and
builds the C++ host.

Manual build:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
