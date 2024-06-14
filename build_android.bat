cd android || goto :error
./gradlew.bat build

cd ..

exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%