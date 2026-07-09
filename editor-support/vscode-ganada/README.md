# 가나다 VS Code 문법 하이라이트

한글 프로그래밍 언어 **가나다**(소스 확장자 `.ㄱㄴㄷ`)를 위한 최소 VS Code 확장. TextMate 문법으로 키워드·내장함수·문자열·숫자·주석을 색칠한다.

## 하이라이트 대상
- **키워드**(`keyword.control`): 함수 만약 아니면 동안 반복 반환 멈춤 계속 시도 잡기 람다 가져오기
- **연산 예약어**(`keyword.operator.word`): 부터 까지 에서 를 그리고 또는 아니다
- **상수**(`constant.language`): 참 거짓 없음
- **내장함수**(`support.function.builtin`): 가나다.py `BUILTINS` 의 115개 전부(출력·질문·서버·저장소·문서 등)
- **문자열** `"..."`(이스케이프 `\n \t \" \\`), **숫자**, **주석** `#`
- 언어 설정: 줄 주석 `#`, 괄호 `{} [] ()` 자동 닫기/짝맞춤

## 소스에서 설치

### 방법 A — 폴더째 설치
```bash
# 이 폴더에서
code --install-extension "$(pwd)"
```
(일부 버전은 `.vsix` 만 받는다. 그 경우 방법 B 또는 방법 C 를 쓴다.)

### 방법 B — 확장 폴더로 복사
이 `vscode-ganada/` 폴더를 사용자 확장 디렉터리에 복사하고 VS Code 재시작:
```bash
cp -R . ~/.vscode/extensions/vscode-ganada-0.1.0
```

### 방법 C — 개발 호스트(F5)
1. VS Code 로 이 `vscode-ganada/` 폴더를 연다.
2. `F5` 를 눌러 Extension Development Host 를 띄운다.
3. 새 창에서 아무 `.ㄱㄴㄷ` 파일을 열면 하이라이트가 적용된다.

## 확인
`.ㄱㄴㄷ` 파일(예: 저장소의 `examples/한몸.ㄱㄴㄷ`)을 열어 색칠을 확인한다.
토큰 스코프는 명령 팔레트 `Developer: Inspect Editor Tokens and Scopes` 로 검사할 수 있다.

패키징/게시는 하지 않는다 — 이 폴더의 소스만으로 로컬에서 로드된다.
