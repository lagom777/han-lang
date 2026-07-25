# 가나다 웹 앱 배포 — C 네이티브 구현. 런타임에 파이썬이 없다(libc/libm 만 쓴다).
# 빌드 단계에서 컴파일하고, 실행 이미지에는 바이너리와 앱 파일만 담는다.

FROM debian:stable-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# Makefile 과 src 만 복사한다 — 호스트에서 빌드한 ganada 바이너리까지 들고 오면
# make 가 최신이라 판단해 다시 안 만들고 다른 플랫폼 바이너리가 실려버린다.
COPY ganada-c/Makefile ./ganada-c/Makefile
COPY ganada-c/src ./ganada-c/src
RUN make -C ganada-c install DESTDIR=/out PREFIX=/usr/local

FROM debian:stable-slim
# 실행 이름 두 개(가나다 · ganada)가 함께 들어온다
COPY --from=build /out/usr/local/bin/ /usr/local/bin/
WORKDIR /app
COPY examples/웹.ㄱㄴㄷ ./examples/웹.ㄱㄴㄷ

# examples/웹.ㄱㄴㄷ 의 서버(8000, ...) 와 맞춤. 서버는 0.0.0.0 에 바인딩됨.
EXPOSE 8000

CMD ["ganada", "실행", "examples/웹.ㄱㄴㄷ"]
