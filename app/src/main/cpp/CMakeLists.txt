cmake_minimum_required(VERSION 3.4.1)

add_library(hello-jni SHARED
            hello-jni.c
            sles_simple_audio.c
            audio_buffer_ring.c
            )

add_library(lib_sassvz SHARED IMPORTED)
set_target_properties(lib_sassvz PROPERTIES IMPORTED_LOCATION
    /Users/audiblereality001/Documents/development/sa_spat_synervoz_lib/.android_products/lib/${ANDROID_ABI}/libsassvz.so)

target_include_directories(hello-jni PRIVATE
                           /Users/audiblereality001/Documents/development/sa_spat_synervoz_lib/.android_products/include)

# Include libraries needed for hello-jni lib
target_link_libraries(hello-jni
                      android
                      OpenSLES
                      lib_sassvz
                      log)
