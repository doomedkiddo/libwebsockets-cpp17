rm -rf build/
mkdir -p build
cd build

pip install conan -i  https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple

conan install .. --output-folder=. --build=missing --lockfile=../conan.lock
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .

