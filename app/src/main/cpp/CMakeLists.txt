cmake_minimum_required(VERSION 3.4.1)

add_library(hello-jni SHARED
            hello-jni.c
            /Users/audiblereality001/Documents/scraps/simple_audio_output_android/sles_simple_audio.c
            )

# Include libraries needed for hello-jni lib
target_link_libraries(hello-jni
                      android
                      OpenSLES
                      log)
