cd android || goto :error
rd /s /q "assets\shaders"
./gradlew.bat build

cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%