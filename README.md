# cp_api
To install, cd into source and type "make install" to install into /usr/local by default. To change install location, change PREFIX variable in src/Makefile, or run "make so" to generate the shared library, where you can copy that to the apropriate location. Once the header file and shared object are installed, use by including cp_api.h into your source file, and look in the examples directory to see how to use the library. Also, when compiling, use the flag "-lcp_api"

