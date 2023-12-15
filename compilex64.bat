clang -m64 main.c -I"D:\Libraries\include" -L"D:\Libraries\SDL_lib\x64" -lSDL2main -lSDL2 -Xlinker /subsystem:windows -o ".\x64\build.exe"
PAUSE