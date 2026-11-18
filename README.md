# Auto Play (Geode 모드)

지메가 알아서 플레이하는 모드. 화면을 보는 게 아니라 **게임 안의 진짜 히트박스**를 읽어서, 히트박스 표시를 안 켜도 됨.

## 빌드
1. 이 폴더 안의 파일을 전부 GitHub repo에 올리기 (`.github` 폴더 포함)
2. Actions → Build Geode Mod → 끝나면 Artifacts에서 `AutoPlay-geode` 다운
3. 안에 있는 `yujun566.autoplay.geode`를 `Geometry Dash/geode/mods`에 넣기

GD 2.2081 + Geode v5.11.2 기준.

## 쓰는 법
- 맵 들어가면 바로 자동으로 플레이
- 일시정지 메뉴 위쪽 `Auto Play: ON/OFF` 버튼으로 켜고 끄기
- 모드 설정
  - Look-ahead: 점프를 몇 초 앞까지 계산할지 (기본 1.0)
  - Flying aim distance: 비행기/웨이브/UFO가 얼마나 앞을 보고 조종할지 (기본 0.22)

## 되는 것
큐브, 로봇, 볼, 스파이더, 비행기, UFO, 웨이브, 스윙 / 노랑·분홍·빨강·파랑·초록·검정 공점 / 발판 / 중력·속도·크기 포탈

## 안 되는 것
대시 공점, 텔레포트, 빠르게 움직이는 장애물, 듀얼, 플랫포머

## 참고
봇으로 깬 건 진짜 클리어로 저장 안 되게 해놨음 (스타트 포지션으로 깬 것처럼 처리).
