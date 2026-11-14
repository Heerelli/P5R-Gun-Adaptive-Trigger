# P5R Gun Adaptive Trigger

**페르소나5 더 로열 (Steam PC)** 의 전투에서 총을 쏠 때, 쏜 캐릭터에 따라 **듀얼센스 적응형 트리거** 효과와 **햅틱 반동**이 나오게 하는 Reloaded-II 모드입니다.

- **조준 중에는 R2로 발사합니다.** 전투에서 방향키 ↑로 총을 들면, R2가 ✕ 대신 발사 버튼이 됩니다. R2를 당기면 "뻑뻑하다가 딸깍" 하는 지점에서 총이 나갑니다. 조준이 아닐 때 R2는 원래대로(바톤 패스, 던전 대시) 동작합니다. ✕로 쏘는 것도 그대로 됩니다.

- **다른 모드가 필요 없습니다.** Persona Essentials, p5rpc.lib, Reloaded.Hooks 같은 라이브러리 모드도, DSX(DualSenseX) 같은 외부 앱도 쓰지 않습니다. Reloaded-II는 DLL을 불러오는 역할만 합니다.
- 듀얼센스에 HID 출력 리포트를 직접 보냅니다. 트리거는 USB·블루투스 모두, 햅틱은 **USB 유선에서만** 됩니다.

> ⚠️ **현재 상태: v0.2 테스트 버전.** 빌드와 단위 테스트는 자동으로 확인됩니다. 실제 게임에서는 아직 테스트하지 않았습니다.
> 특히 **"누가 쐈는지" 판별은 첫 실행 때 자동으로 학습합니다** (아래 [캐릭터 판별](#캐릭터-판별) 참고). 학습 전에는 모든 총에 기본 효과가 나옵니다.

## 빠른 설치

1. [Releases](https://github.com/Heerelli/P5R-Gun-Adaptive-Trigger/releases) 에서 `P5R-Gun-Adaptive-Trigger-ReloadedII.zip` 을 받습니다. 아직 릴리스가 없으면 [Actions](https://github.com/Heerelli/P5R-Gun-Adaptive-Trigger/actions) 의 최근 빌드에서 아티팩트를 받으세요.
2. zip을 Reloaded-II 창에 끌어다 놓습니다. 직접 빌드했다면 `p5rpc.gun.adaptivetrigger` 폴더를 `Reloaded-II\Mods\` 에 복사합니다.
3. Reloaded-II에서 Persona 5 Royal(`P5R.exe`)을 고르고 **P5R Gun Adaptive Trigger** 를 체크합니다.
4. 듀얼센스를 연결하고 **Reloaded-II에서 Launch Application** 으로 실행합니다.
5. 게임 중 **F9** 를 누르면 캐릭터 효과(트리거 + 햅틱)를 하나씩 차례로 시험해 볼 수 있습니다.
6. 전투에서 **방향키 ↑ → R2를 끝까지 당기기** 로 쏴 봅니다.

## 캐릭터별 기본 효과

| 캐릭터 | INI 섹션 | 총 | 쏘는 순간 (Kick, R2) | 쏜 뒤 (Rest, R2) | 햅틱 반동 (USB) |
| --- | --- | --- | --- | --- | --- |
| 조커 | `[Joker]` | 권총 | 짧고 날카로운 "탁" | 중간 저항 뒤 딸깍 | 짧고 단단한 "톡" |
| 류지 | `[Skull]` | 샷건 | 낮고 길고 묵직한 진동 | 가장 센 딸깍 | "탕" + 크게 울리는 "쿵" |
| 모르가나 | `[Mona]` | 새총 | 가벼운 "틱" | 고무줄처럼 당기는 저항 | 가벼운 "팅" |
| 안 | `[Panther]` | 기관단총 | 덜덜거리는 연사 진동 | 가벼운 딸깍 | "타타탁" 3연발 |
| 유스케 | `[Fox]` | 라이플 | 깊은 "탕" | 깊은 곳에서 센 딸깍 | 날카로운 "탕" + 깊은 여운 |
| 마코토 | `[Queen]` | 리볼버 | 묵직한 "탕" | 깊은 곳에서 딸깍 | 묵직한 "탕" |
| 하루 | `[Noir]` | 유탄발사기 | 둔탁하고 긴 "쿵" | 끝까지 뻑뻑한 저항 | "펑" 발사 → 잠시 뒤 "쾅" |
| 아케치 | `[Crow]` | 레이건 | 가늘고 높은 전기 진동 | 약한 저항 | 내려가는 "위잉" |
| 카스미 | `[Violet]` | — | 또렷한 "탁" | 중간 저항 뒤 딸깍 | 또렷한 "톡" |
| (판별 전/모름) | `[Default]` | — | 조커와 같음 | 조커와 같음 | 조커와 같음 |

트리거는 이렇게 바뀝니다:

```text
방향키 ↑ (조준) ─► AimEffect (R2: 뻑뻑하다가 딸깍) ─► 발사 ─► Kick ─► Rest ─► 조준 끝 ─► 원래대로
```

- 총알 1발이 소모될 때마다 Kick과 햅틱이 나옵니다.
- Down Shot, Bullet Hail 처럼 총알을 쓰는 기술에도 반응합니다.

## 요구 사항

| 항목 | 내용 |
| --- | --- |
| 게임 | Persona 5 Royal, Windows **Steam판** (`P5R.exe`) |
| 모드 로더 | [Reloaded-II](https://github.com/Reloaded-Project/Reloaded-II) |
| 컨트롤러 | 듀얼센스 / 듀얼센스 엣지 (USB 또는 블루투스) |
| OS | Windows 10/11 64bit |

## 동작 확인 (로그)

모드 폴더(`Reloaded-II\Mods\p5rpc.gun.adaptivetrigger\`)의 **`P5RGunTrigger.log`** 를 엽니다. 실행할 때마다 새로 만들어집니다.

| 로그 줄 | 뜻 |
| --- | --- |
| `DualSense found (USB, ...)` | 컨트롤러를 찾았습니다 |
| `hooked gun shot at P5R.exe+0x...` | 총 발사 코드를 찾아 연결했습니다 |
| `SHOT unit=... bullets 8->7 shooter=Skull` | 총을 쐈고, 쏜 사람을 판별했습니다 |
| `input: the game reads the pad through XInput` (또는 `DualSense HID`) | 게임의 패드 입력을 찾았습니다. 조준 중 R2 발사가 동작합니다 |
| `AIM start` / `AIM end` | 조준 시작·끝을 인식했습니다 |
| `HAPTICS ACTIVE` | 햅틱이 켜졌습니다 (USB) |
| `LEARNED unit id offset +0x...` | 캐릭터 판별 위치를 학습했습니다 (INI에 저장됨) |
| `ERROR: gun shot code not found` | 게임 버전이 다르거나, 다른 모드·치트(무한 총알 등)가 같은 코드를 바꿔 둔 상태 |

## 캐릭터 판별

총을 쏘는 코드는 **"쏜 캐릭터의 전투 유닛"** 을 알려 주지만, 그 유닛 안에서 캐릭터 ID가 어디에 있는지는 공개된 자료가 없습니다. 그래서 `Mode=auto` 에서는 직접 학습합니다.

1. 전투에서 **서로 다른 캐릭터 3명 이상**으로 총을 한 번씩 쏩니다.
2. 모든 사수에서 1~10 사이 값이고, 같은 유닛에서는 바뀌지 않으며, 캐릭터마다 다른 값인 필드가 **딱 하나** 남으면 그 위치를 씁니다.
3. 학습한 위치는 `P5RGunTrigger.ini` 의 `[Character] LearnedOffset` 에 저장되어 다음부터 바로 쓰입니다.

학습이 안 되거나(로그에 `learning: ... candidate field(s)` 만 계속 나옴) 캐릭터가 틀리게 나오면:
- 로그의 `unit ... dump` 와 `SHOT` 줄을 [Issues](https://github.com/Heerelli/P5R-Gun-Adaptive-Trigger/issues) 에 올려 주세요. 위치를 찾아 기본값으로 넣겠습니다.
- 직접 정하려면 `Mode=offset`, `UnitIdOffset=0x...` 로 지정합니다.
- 잘못 학습했다면 `LearnedOffset=` 줄을 지우면 다시 학습합니다.

## 설정 (`P5RGunTrigger.ini`)

게임 중 **F10** 을 누르면 다시 읽습니다.

| 섹션 / 키 | 기본값 | 설명 |
| --- | --- | --- |
| `[General] Enabled` | `1` | 0이면 모드 끄기 |
| `Trigger` | `R2` | `R2`, `L2`, `Both` |
| `AimEffect` | `weapon 3 6 5` | 조준 중(첫 발 전) R2 효과 |
| `AimReleaseMs` | `150` | 조준이 끝나고 트리거를 푸는 시간(ms) |
| `HoldMs` | `2500` | 패드 입력을 못 찾았을 때만: 마지막 발사 후 Rest 유지 시간(ms) |
| `RefreshMs` | `400` | 효과가 켜져 있는 동안 다시 보내는 간격(ms). 다른 프로그램이 덮어써도 복구됨 |
| `MuteWhenUnfocused` | `1` | 게임 창이 비활성이면 트리거 해제 |
| `TestHotkeys` | `1` | F9 = 다음 캐릭터 효과 시험, F10 = INI 다시 읽기 |
| `LogShots` | `1` | 발사마다 로그 기록 |
| `[Input] R2FiresWhileAiming` | `1` | 조준 중 R2로 발사. 0이면 끔 |
| `R2FireThreshold` | `170` | 발사로 치는 R2 깊이 (0~255). `AimEffect`의 딸깍 지점과 맞춰 둠 |
| `AimAfterFireMs` | `1500` | 손을 뗀 뒤에도 조준으로 보는 시간(ms) |
| `AimTimeoutMs` | `20000` | 아무 입력이 없으면 조준을 끝내는 시간(ms) |
| `[Haptics] Enabled` | `1` | 햅틱 반동 (USB) |
| `Gain` | `1.0` | 전체 햅틱 세기 (0.0~2.0). 캐릭터별로는 각 섹션의 `Haptic=` |
| `DeviceName`, `BufferMs`, `LeftChannel`, `RightChannel` | | SMTVV DualSense Haptic과 같은 오디오 설정 |
| `[Character] Mode` | `auto` | `auto` (학습), `offset` (UnitIdOffset 사용), `off` (모두 `[Default]`) |
| `UnitIdOffset` | `-1` | 캐릭터 ID 위치를 직접 지정 (예: `0x2`) |
| `DumpUnits` / `DumpBytes` | `1` / `0x200` | 새 사수의 유닛 앞부분을 로그에 덤프 |

### 효과 문법 (캐릭터 섹션의 `Kick`, `Rest`)

| 문법 | 느낌 |
| --- | --- |
| `off` | 효과 없음 |
| `feedback <시작 0-9> <세기 1-8>` | 시작 위치부터 끝까지 일정한 저항 |
| `weapon <시작 2-7> <끝 3-8> <세기 1-8>` | 총 방아쇠: 뻑뻑하다가 "딸깍" 하고 풀림 |
| `vibration <시작 0-9> <세기 1-8> <Hz 1-255>` | 트리거 진동. 시작 0이면 손가락만 얹어도 느껴짐 |
| `machine <시작 0-8> <끝 1-9> <세기A 0-7> <세기B 0-7> <Hz> <주기 x0.1초>` | 두 세기를 번갈아 내는 진동 (연사 느낌) |

`KickMs` 는 Kick을 유지하는 시간(ms), `Haptic` 은 그 캐릭터의 햅틱 세기(0.0~2.0, 0이면 없음)입니다.

## 조준 중 R2 발사는 어떻게 판단하나

게임 안의 "조준 중" 상태를 직접 읽지 않고, 버튼 입력으로 판단합니다.

- **조준 시작:** 방향키 ↑
- **조준 끝:** ○(뒤로), △, □, L1, R1, OPTIONS, 방향키 ↓ 중 하나를 누를 때 / 발사 후 손을 떼고 `AimAfterFireMs` 가 지났을 때 / `AimTimeoutMs` 동안 아무 입력이 없을 때
- 조준 중 R2를 `R2FireThreshold` 이상 당기면 게임에는 ✕로 전달되고 R2는 눌리지 않은 것으로 보입니다.
- 조준이 끝날 때 R2를 쥐고 있었다면, 손을 뗄 때까지 R2를 숨깁니다. 그래서 바톤 패스가 실수로 나가지 않습니다.

메뉴에서 방향키 ↑를 누르면 조준으로 잘못 볼 수 있습니다. 메뉴는 보통 ○로 닫으니 그때 풀립니다. 그래도 문제가 되면 알려 주세요.

게임이 패드를 읽는 두 경로를 모두 지원합니다.
- **XInput:** Steam Input이 켜져 있거나 Xbox 패드를 쓸 때
- **듀얼센스 HID:** Steam Input이 꺼져 있을 때

어느 쪽인지는 로그의 `input: the game reads the pad through ...` 줄에 나옵니다.

## 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| F9를 눌러도 트리거가 안 움직임 | 로그에 `DualSense found` 가 있는지. Steam Input을 끄고 다시 시도 (아래 참고) |
| F9는 되는데 총을 쏴도 안 됨 | 로그에 `hooked gun shot` 이 있는지. 무한 총알 치트나 같은 코드를 바꾸는 모드를 끄기 |
| 효과가 잠깐 났다가 바로 풀림 | Steam Input이 출력을 덮어쓰는 경우. `RefreshMs` 를 줄이거나 Steam Input 끄기 |
| 모든 캐릭터가 같은 느낌 | 아직 학습 전(`shooter=unknown`). [캐릭터 판별](#캐릭터-판별) 참고 |
| R2로 안 쏴짐 | 로그에 `input: the game reads the pad` 가 있는지, ↑ 누를 때 `AIM start` 가 찍히는지. 없으면 로그를 Issues에 올려 주세요 |
| 조준이 아닌데 R2가 ✕처럼 동작 | 메뉴에서 ↑를 눌러 조준으로 잘못 본 경우. ○를 한 번 누르면 풀립니다 |
| 햅틱이 안 남 | USB 유선인지, 사운드 설정에 "Wireless Controller" 가 있는지, 로그에 `HAPTICS ACTIVE` 가 있는지 |
| 게임 종료 후 트리거가 뻑뻑함 | 컨트롤러를 뺐다 꽂거나, 게임을 다시 켰다 끄면 풀립니다 |

> **Steam Input**: 켜 둬도 동작하도록 트리거 관련 플래그만 보냅니다. 그래도 Steam이 같은 컨트롤러에 계속 출력을 보내므로 효과가 풀린다면, 라이브러리에서 P5R 우클릭 → 속성 → 컨트롤러 → **Steam Input 끄기** 를 시도하세요.

## 직접 빌드 (개발자용)

Releases의 zip은 GitHub Actions(`.github/workflows/build.yml`)가 만듭니다. 직접 빌드하려면 **"x64 Native Tools Command Prompt for VS 2022"** 에서:

```bat
build_reloaded.bat
```

결과물: `out\reloaded\p5rpc.gun.adaptivetrigger\` (`P5RGunTrigger.dll`, `ModConfig.json`, `P5RGunTrigger.ini`)

단위 테스트 (리포트 바이트, CRC, 효과 인코딩, 조준 판단, 입력 리맵, 후킹 스텁 디스어셈블):

```sh
sh tests/check_stub.sh
```

## 동작 원리 (개발자용)

```text
P5R.exe 전투 코드 (총알 1발 소모)
    66 FF C8                  dec ax
    66 41 89 87 D0 79 00 00   mov [r15+79D0h], ax      ; r15 = 쏜 전투 유닛
        │  (11바이트를 jmp + NOP으로 교체)
        ▼
  스텁: 레지스터·플래그·xmm0-5 저장 → OnShot(r15, ax) → 복원 → 원래 두 명령 실행 → 복귀
        │  링 버퍼 (게임 스레드에서는 기록만)
        ▼
  작업 스레드: 유닛 → 캐릭터 ID → [캐릭터] 효과 → 듀얼센스 출력 리포트
                                     │            USB 0x02 (48바이트) / BT 0x31 (78바이트 + CRC32)
                                     └─► 햅틱 합성 → WASAPI → USB 오디오 3·4번 채널 (보이스코일)

P5R.exe 의 import 테이블만 패치 (우리 DLL과 다른 모듈은 그대로)
  XInputGetState / GetProcAddress("XInputGetState")        ┐
  CreateFile* · ReadFile · GetOverlappedResult* · HidD_GetInputReport (듀얼센스 핸들만) ┘
        └─► AimTracker (↑ = 조준, ○ 등 = 끝) ─► 조준 중이면 R2 → ✕
```

- 소스 구성: `src/dllmain.cpp` (발사 후킹, 트리거, 캐릭터 판별), `src/inputhook.cpp` (입력 후킹), `src/haptics.cpp` (햅틱), `src/effects.h` · `src/input.h` · `src/hookstub.h` (테스트 가능한 순수 로직)

- 후킹 위치는 바이트 패턴으로 찾습니다 (P5R 치트 테이블의 "무한 총알"이 2022년부터 쓰는 패턴). 실행 파일이 아직 풀리지 않았을 수 있어서, 패턴이 보일 때까지 최대 2분 동안 다시 찾습니다.
- 출력 리포트에는 트리거 플래그만 켭니다. 진동, 라이트바, LED는 게임이나 Steam이 설정한 그대로 둡니다.
- 트리거 효과 인코딩은 [Nielk1의 TriggerEffectGenerator](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db) 와 같은 방식입니다.

## 앞으로 할 일

- [x] 자동 빌드와 단위 테스트
- [ ] 실제 게임에서 테스트
- [ ] 캐릭터 ID 위치 확정 후 기본값으로 넣기
- [x] 조준 중에만 R2로 발사 + R2 저항
- [x] 캐릭터별 햅틱 반동 (USB)
- [ ] 게임 내부의 조준 상태를 직접 읽어서 판단 정확도 높이기
- [ ] 캐릭터별 효과 다듬기

## 크레딧 · 라이선스

- 총알 소모 명령 패턴: FearLess Cheat Engine 포럼의 P5R 치트 테이블 커뮤니티
- 햅틱 엔진: [SMTVV DualSense Haptic](https://github.com/Heerelli/SMTVV-Dualsence-Haptic) (같은 작성자, MIT)
- 이 저장소의 코드: MIT License (`LICENSE`)
- Persona 5 Royal은 ATLUS/SEGA의 상표입니다. 이 모드는 비공식 팬 모드입니다.
