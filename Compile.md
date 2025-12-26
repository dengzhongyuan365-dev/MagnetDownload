编译步骤

Linux:

// 生成CMAKE配置文件
cmake -B build -DCMAKE_BUILD_TYPE=Debug .
// 编译
cmake --build build --config Debug -j 8