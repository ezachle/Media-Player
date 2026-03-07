# Media-Player
Created using ffmpeg to decode the audio and SDL3 to output to a window.

## Library Versions
```
Name            : SDL3
Epoch           : 0
Version         : 3.4.0
Release         : 3.fc43
Architecture    : x86_64
Installed size  : 3.0 MiB
Source          : SDL3-3.4.0-3.fc43.src.rpm
From repository : updates
Summary         : Cross-platform multimedia library
URL             : http://www.libsdl.org/
License         : Zlib AND MIT AND Apache-2.0 AND (Apache-2.0 OR MIT)
Description     : Simple DirectMedia Layer (SDL) is a cross-platform multimedia library designed
                : to provide fast access to the graphics frame buffer and audio device.
Vendor          : Fedora Project

Name            : ffmpeg
Epoch           : 0
Version         : 7.1.2
Release         : 7.fc43
Architecture    : x86_64
Installed size  : 2.5 MiB
Source          : ffmpeg-7.1.2-7.fc43.src.rpm
From repository : rpmfusion-free
Summary         : Digital VCR and streaming server
URL             : https://ffmpeg.org/
License         : GPLv3+
Description     : FFmpeg is a complete and free Internet live audio and video
                : broadcasting solution for Linux/Unix. It also includes a digital
                : VCR. It can encode in real time in many formats including MPEG1 audio
                : and video, MPEG4, h263, ac3, asf, avi, real, mjpeg, and flash.
Vendor          : RPM Fusion
```

## Compile
To compile, run
```
cmake -B build
cmake --build build
cd build
./MediaPlayer <path>
```
