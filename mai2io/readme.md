Affine IO-Mai

本IO使用与chuniio相似的自定义的串口协议来控制Mai上的8个外围按键以及test、service、coin按键，相对比直接键盘映射，延迟更低。

编译DLL文件：(注意需要使用支持64位的GCC)

`gcc -m64 -shared .\mai2io.c .\config.c .\serial.c -o mai2io_affine.dll`

在Segatool中使用：

`[mai2io]
path=mai2io_affine.dll`


