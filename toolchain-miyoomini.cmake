# Toolchain file para cross-compilar para o Miyoo Mini / Mini Plus
# (Cortex-A7, ARMv7 hard-float) usando o union-miyoomini-toolchain.
#
# Uso:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-miyoomini.cmake ...
#
# Baseado nas flags usadas com sucesso por outros projetos (ex. 32blit-sdk)
# no mesmo toolchain: https://github.com/shauninman/union-miyoomini-toolchain

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(COMMON_FLAGS "-Os -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7ve+simd")
set(CMAKE_C_FLAGS_INIT   "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS}")

# Ajuste este caminho para o sysroot do seu toolchain, caso o build reclame
# de headers/libs do sistema não encontrados. Dentro do container do
# union-miyoomini-toolchain isso normalmente já está resolvido via PATH,
# mas fica aqui como referência caso você use o buildroot diretamente:
# set(CMAKE_FIND_ROOT_PATH /root/buildroot/output/host/arm-buildroot-linux-gnueabihf/sysroot)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
