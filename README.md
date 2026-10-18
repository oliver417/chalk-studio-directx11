# CHALK Studio

Windows용 DirectX 11 칠판 드로잉 프로그램입니다. 마우스로 그림을 그리고 획의 좌표·색·굵기를 `.chalk` 파일에 저장하고 다시 불러올 수 있습니다.

## 실행

압축을 푼 뒤 `bin/Release/ChalkStudio.exe`를 실행합니다. Windows 10/11 x64에서 사용하며 별도 라이브러리 설치가 필요하지 않습니다. 실행 파일과 `samples` 폴더는 프로젝트 안의 위치를 유지해 주세요.

상단의 **예제 열기**를 누르면 동봉된 풍경 예제를 볼 수 있습니다. 빈 칠판에서 시작하려면 프로그램을 다시 실행하거나 **전체 지우기**를 사용합니다.

## 사용법

| 동작 | 조작 |
|---|---|
| 그리기 | 칠판 안에서 마우스 왼쪽 버튼을 누른 채 이동 |
| 분필 / 지우개 | 도구 버튼 또는 B / E |
| 색상 | 색상 버튼 또는 1~6 |
| 굵기 | 3px / 7px / 14px 버튼 |
| 저장 | 저장 버튼 또는 Ctrl+S |
| 다른 이름으로 저장 | Ctrl+Shift+S |
| 불러오기 | 불러오기 버튼 또는 Ctrl+O |
| 마지막 획 되돌리기 | 되돌리기 버튼 또는 Ctrl+Z |
| 전체 지우기 | 전체 지우기 버튼 또는 Ctrl+N, 확인 후 실행 |
| 예제 열기 / 도움말 | F2 / F1 |
| 키보드로 버튼 선택 | Tab / Shift+Tab 이동 후 Space |

지우개도 획으로 기록되므로 Ctrl+Z로 되돌릴 수 있습니다. 칠판 밖으로 드래그하면 경계에서 획을 끝냅니다. 저장하지 않은 상태로 파일을 열거나 프로그램을 닫으면 저장 여부를 묻습니다.

## 빌드

Visual Studio 2022의 **C++를 사용한 데스크톱 개발**과 Windows 10/11 SDK가 필요합니다.

1. `ChalkStudio.sln`을 엽니다.
2. `Release | x64`를 선택해 솔루션을 빌드합니다.
3. 시작 프로젝트를 `ChalkStudio`로 선택하고 실행합니다.

명령줄에서는 `build.bat`를 실행하면 Release 빌드와 데이터 테스트가 순서대로 실행됩니다. Debug 구성도 포함되어 있습니다. 다른 컴퓨터의 빌드 도구 경로를 하드코딩하지 않으며 `vswhere.exe`로 설치 위치를 찾습니다.

## 구조

```text
ChalkStudio.sln              Visual Studio 솔루션
ChalkStudio.vcxproj          앱 프로젝트
src/main.cpp                DirectX 초기화, 화면, 버튼, 마우스 입력
src/Board.h                 획 데이터와 저장·불러오기 선언
src/Board.cpp               파일 검증, 임시 파일 저장, 복원
tests/BoardTests.cpp         저장·복원과 잘못된 파일 테스트
tests/BoardTests.vcxproj     테스트 프로젝트
samples/풍경_예제.chalk       동봉 예제
screenshots/                실제 실행 화면 2장
bin/Release/ChalkStudio.exe  실행 파일
docs/                       제출용 개발 보고서와 AI 질문·결과 기록
```

## 그래픽 구현

`D3D11CreateDevice`로 Direct3D 11 장치를 만들고 DXGI 스왑 체인의 화면 버퍼를 Direct2D 1.1 렌더 타깃으로 사용합니다. Direct2D가 선과 도형을, DirectWrite가 텍스트를 그립니다. GPU 장치 생성이 실패하면 DirectX 11 WARP를 사용합니다. 창 크기와 DPI가 바뀔 때 타깃을 다시 만들며, 그림은 1200×720의 비율을 유지합니다.

그림은 `Drawing → Stroke → Point` 구조로 보관합니다. 좌표는 0~1로 정규화해 창 크기가 달라져도 비율이 유지됩니다. 각 획에 색, 굵기, 지우개 여부를 기록합니다. 단일 클릭은 원점으로 표시하고 연속 입력은 끝이 둥근 선분으로 연결합니다. 지우개는 단색 칠판의 배경색을 덧그립니다.

## 저장 형식과 검증

텍스트 파일의 첫 줄은 `CHALKBOARD 1`, 다음 줄은 획 수입니다. 각 획은 `색상 정수 굵기 지우개 여부 좌표 수`와 좌표 행으로 구성됩니다. 소수점은 시스템 언어 설정과 관계없이 `.`을 사용합니다.

```text
CHALKBOARD 1
1
16052702 7 0 2
0.1 0.2
0.5 0.6
```

불러오기는 파일 크기 32MB, 획 20,000개, 전체 좌표 1,000,000개를 제한하고 좌표 범위, 색상, 유한한 굵기와 파일 버전을 검증합니다. 검사에 실패하면 기존 칠판을 유지합니다. 저장은 같은 폴더의 임시 파일에 기록한 다음 기존 파일을 교체합니다. `.chalk`는 프로그램 전용 데이터 형식이며 일반 이미지 파일이 아닙니다.

## 테스트

`bin/Release/BoardTests.exe`는 왕복 저장·복원, 한글 경로, 빈 그림, 덮어쓰기, 잘못된 파일 12종, 실패 시 기존 데이터 보존, 없는 파일과 폴더를 확인합니다. 테스트 파일은 실행한 폴더의 `.build` 아래에서 생성하고 정리합니다.

실행 화면은 실제 앱을 캡처한 이미지입니다. 풍경 예제의 기본 99개 획은 AI가 생성한 좌표 데이터이며, 실제 마우스 입력으로 추가한 획도 저장·복원됩니다. AI의 사용 범위와 실제 질문·결과는 제출 ZIP의 `docs`에 기록했습니다.

## 참고한 공식 문서

- [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)
- [CreateBitmapFromDxgiSurface](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1devicecontext-createbitmapfromdxgisurface%28idxgisurface_constd2d1_bitmap_properties1_id2d1bitmap1%29)
- [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
