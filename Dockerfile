# 가나다 웹 앱 배포 — 외부 의존성 0 (파이썬 표준 라이브러리만)
FROM python:3.12-slim

WORKDIR /app

# 인터프리터와 웹 앱만 복사
COPY 가나다.py ./
COPY examples/웹.ㄱㄴㄷ ./examples/웹.ㄱㄴㄷ

# examples/웹.ㄱㄴㄷ 의 서버(8000, ...) 와 맞춤. 서버는 0.0.0.0 에 바인딩됨.
EXPOSE 8000

CMD ["python3", "가나다.py", "실행", "examples/웹.ㄱㄴㄷ"]
