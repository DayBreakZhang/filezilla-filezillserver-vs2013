# filezilla-filezillserver-vs2013
FileZilla 是一个免费而高性能的 FTP 客户端软件,使用vs2013进行编译

filezilla 的官方网址是：https://filezilla-project.org/

filezilla 所依赖的工程：

       1.wxWidgets

              版本：3.0.2

              下载地址：点击打开链接

               说明和编译：

                           1.最好自己拿vs编译一下。

                           2.在wxMSW-3.0.2/build/msw下面有vs的工程文件

                           3. 编译的时候出现Cannotopen include file: 'wx/setup.h': No such file or directory

                                        将E:/workspace/wxMSW-2.8.12/include/wx/msw/setup.h

                                        拷贝至上一级目录

                                        E:/workspace/wxMSW-2.8.12/include/wx/setup.h

        2.GnuTLS

               版本：3.3.13

               下载地址：点击打开链接

               说明和编译：

                           1.这个不用自己编译，可以直接下载win的版本

                           2.如果在编译客户端的时候出面找不到gnutls_free这个函数的话，打开libgnutls-28.def这个文件将 gnutls_free @XXX 后面的DATA去掉

                           3.下载win版本的时候，是没有lib文件的，但是有def文件，可以使用vs的命令行进行转换，转换命令：

                                                      lib /libgnutls-28.def

        3.sqlite

                 版本：amalgamation-3080803

                 下载：点击打开链接

                 说明和编译：

                            1.和上面一们下载win的版本是没有lib文件，但是有def文件使用命令转一下就可以
filezilla server所依赖的工程

             1.zlib

                  版本：128

                  下载地址：点击打开链接

                  说明和编译

                           1.这个可以自己编译
                           2.注意zlib的c/c++代码生成中的运行库是mt/md是否和filezilla中的相同

            2.openss

                       版本：1.0.1e

vs2013编译的其它问题

            1.怎样解决VS2013模块对于SAFESEH 映像是不安全的

                        链接器”--“命令行”将 /SAFESEH:NO 复制到“其它选项（D)”框中，然后点击应用



[http://blog.csdn.net/a406226715/article/details/44199559](http://blog.csdn.net/a406226715/article/details/44199559 "详情")