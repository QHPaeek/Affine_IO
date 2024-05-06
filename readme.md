Affine IO

本IO使用与官方串口协议相似的协议连接手台，但是在协议内添加了一些自定义命令，让同一个串口实现JVS板功能，如IR KEY，灯光，以及未来可能支持的Coin Key。

编译测试exe程序：

`gcc -o test .\test.c .\serialslider.c`

编译DLL文件：

`gcc -shared -o affine.dll .\chuniio.c .\config.c .\serialslider.c`

在Segatool中使用：

`[chuniio]
; If you wish to sideload a different chuniio, specify the DLL path here
path=affine.dll`


