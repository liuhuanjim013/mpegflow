CFLAGS = -O3 -D__STDC_CONSTANT_MACROS
LDFLAGS = -lswscale -lavdevice -lavformat -lavcodec -lswresample -lavutil -lpthread -lbz2 -lz -lc 

OPENCV_DIR=/usr/local/include
OPENCV_LIB=/usr/local/lib

INSTALL_DIR=/usr/local/bin

mpegflow: mpegflow.cpp
	g++ $< -o $@ $(CFLAGS) $(LDFLAGS) $(INSTALLED_DEPS)

vis: vis.cpp
	g++ $< -o $@ $(CFLAGS) -I$(OPENCV_DIR) -L$(OPENCV_LIB) -lopencv_highgui -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -lopencv_core -lpng $(LDFLAGS)

install: mpegflow vis
	install mpegflow $(INSTALL_DIR)
	install vis $(INSTALL_DIR)

uninstall:
	rm -f $(INSTALL_DIR)/mpegflow
	rm -f $(INSTALL_DIR)/vis

clean:
	rm mpegflow vis
