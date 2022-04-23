set CompilerPath="D:\Programming\Windows Kits\10\bin\10.0.19041.0\x64\dxc.exe"

set MeshShaderParams=-O0 -T ms_6_5 -Fo ../build/Debug/MeshletMS.cso MeshletMS.hlsl -Fc ../build/Debug/MeshletMS.asm
set PixelShaderParams=-O0 -T ps_6_5 -Fo ../build/Debug/MeshletPS.cso MeshletPS.hlsl -Fc ../build/Debug/MeshletPS.asm

%CompilerPath% %MeshShaderParams%
%CompilerPath% %PixelShaderParams%

