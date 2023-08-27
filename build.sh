g++ example.cpp coroutine.cpp -lpthread -O3 -o example
if [ $? -eq 0 ]; then
    echo 'build success, input \033[41;04;37m./example\033[0m to run'
else
    echo 'build failed, please check'
fi