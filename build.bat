@echo off
setlocal

set "ROOT=%~dp0"
set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    for /f "usebackq delims=" %%p in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%p"
    if defined VS set "VCVARS=!VS!\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "%VCVARS%" (
    echo [kbl] не найден vcvars64.bat - установите Visual Studio с рабочей нагрузкой "Разработка классических приложений на C++"
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [kbl] не удалось инициализировать окружение MSVC
    exit /b 1
)

if not exist "%ROOT%build" mkdir "%ROOT%build"

pushd "%ROOT%build"
cl /nologo /std:c++20 /utf-8 /O2 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 ^
   "%ROOT%src\util.cpp" "%ROOT%src\value.cpp" "%ROOT%src\lexer.cpp" "%ROOT%src\parser.cpp" ^
   "%ROOT%src\interp.cpp" "%ROOT%src\builtins.cpp" "%ROOT%src\main.cpp" ^
   /Fe:kbl.exe /link user32.lib gdi32.lib
set "ERR=%errorlevel%"
popd

if not "%ERR%"=="0" (
    echo [kbl] сборка не удалась
    exit /b 1
)

echo [kbl] готово: %ROOT%build\kbl.exe
