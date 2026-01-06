#### 编译步骤

Linux:

// 生成CMAKE配置文件
###### cmake -B build -DCMAKE_BUILD_TYPE=Debug .
// 编译

##### cmake --build build --config Debug -j 8

Win: 

##### cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=ON

在vs studio的x64 Native下输入上述命令进行.vcproj文件的生成。然后用对应的打开即可。

#### cmake -B build -G "MinGW Makefiles" -DBUILD_TESTS=ON