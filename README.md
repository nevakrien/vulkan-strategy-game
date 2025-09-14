# vulkan-strategy-game
a small strategy game made in C++ inspired by https://github.com/baeng72/Programming-an-RTS

build and run:
```bash
cmake -S . -B build && cmake --build build -j && ./build/mygame
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ./build/mygame
```

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target build_tests -j
```