Affine IO-Mai

本IO使用与chuniio相似的自定义串口协议来控制Mai上的触摸、8个外围按键以及test、service、coin按键，相对比直接键盘映射，延迟更低。

编译DLL文件：(注意需要使用支持64位的GCC)

```
gcc -m64 -shared .\mai2io.c .\config.c .\serial.c .\dprintf.c -o mai2io_affine.dll -lsetupapi
```

编译测试exe程序：

```
gcc -m64 .\test.c .\serial.c .\dprintf.c -o curva_test.exe -lsetupapi
```

在Segatool中使用：

```
[mai2io]
path=mai2io_affine.dll
```

由于目前版本segatool在触摸和按键两个部分会分别调用两次DLL，导致DLL实际上会被多次加载，因此线程之间的变量必须使用共享内存（或者是命名管道等，进程间同步数据的方法）才能使得mai_io_get_optbtn和gamebtn获取到正常的数据。

1P默认使用COM11

2P默认使用COM12
