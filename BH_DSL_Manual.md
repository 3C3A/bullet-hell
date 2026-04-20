# BH 탄막 스크립트 DSL 상세 설명서

이 문서는 `BH.cpp`의 **현재 파서와 실행 코드 기준**으로 정리한 문법 설명서다.  
아래에 적힌 문법만 실제로 동작한다.  
한 줄은 공백으로 토큰을 나누며, **따옴표 문자열은 지원하지 않는다**.

\---

## 1\. 파일 종류

이 엔진은 크게 세 종류의 텍스트 파일을 읽는다.

### 1-1. 스테이지 파일

예: `stages/stage1/stage.txt`

스테이지 전체 배치와 적 배치를 적는다.

### 1-2. 적 스크립트 파일

예: `stages/stage1/enemy1.txt`

적이 사용할 탄막 패턴을 적는다.

### 1-3. 파워 프로필 파일

예: `ply/power0/config.txt`

플레이어 공격, 폭탄, 표시 설정을 적는다.

\---

## 2\. 공통 규칙

### 2-1. 주석

한 줄의 첫 글자가 `#`이면 주석이다.

```txt
# 이 줄은 무시된다
```

### 2-2. 공백

공백과 탭은 토큰 구분자다.  
여러 공백은 하나처럼 처리된다.

### 2-3. 대소문자

명령어는 대소문자를 크게 가리지 않도록 작성하는 편이 좋다.  
현재 코드는 첫 토큰과 일부 하위 토큰을 소문자로 바꿔 비교한다.

### 2-4. 문자열

따옴표 문자열은 없다.  
파일 경로, 라벨 이름, 변수 이름에는 공백이 들어가면 안 된다.

### 2-5. 경로 제한

파일 경로는 내부 보안 규칙 때문에 다음 조건을 만족해야 한다.

* 상대 경로여야 한다
* `..` 포함 금지
* `:` 포함 금지
* `\\\\`는 내부적으로 `/`로 바꾼다
* 허용 루트는 보통 `stages/`, `ply/`, `scripts/`, `assets/` 아래다

\---

## 3\. 값과 표현식

대부분의 숫자 자리는 **표현식**을 받을 수 있다.

예:

```txt
wait 60
set t 120 + 30
shot aimed 4 + 1
shot aimed 5 rand(-45, 45)
```

### 3-1. 연산자 우선순위

높은 것부터 낮은 것 순서:

1. 괄호 `( )`
2. 단항 `+ - !`
3. 거듭제곱 `^`
4. 곱셈/나눗셈/나머지 `\* / %`
5. 덧셈/뺄셈 `+ -`
6. 비교 `< > <= >=`
7. 같음 `== !=`
8. 논리 AND `\&\&`
9. 논리 OR `||`

### 3-2. 숫자

정수와 실수를 모두 쓸 수 있다.

```txt
wait 30
wait 2.5
shot circle 16 3.0
```

### 3-3. 변수

사용자가 만든 변수와 내장 변수를 사용할 수 있다.

```txt
set phaseTimer 120
sub phaseTimer 1
if phaseTimer > 0
```

### 3-4. 함수 호출

표현식 안에서 함수도 쓸 수 있다.

```txt
set a sin(time)
set d dist(x, y, playerx, playery)
set r rand(-45, 45)
```

\---

## 4\. 내장 변수

다음 이름은 기본으로 읽을 수 있다.  
코드에서는 일부 이름이 `enemy\_x`, `player\_x`처럼 써도 같은 값으로 해석되도록 정규화된다.

### 적 위치 / 상태

* `x`, `enemyx`, `enemy\_x`, `posx`, `px`
* `y`, `enemyy`, `enemy\_y`, `posy`, `py`
* `hp`
* `maxhp`
* `phase`
* `time`

### 플레이어 상태

* `playerx`, `player\_x`
* `playery`, `player\_y`
* `slow`
* `invuln`
* `bombtimer`
* `lives`
* `bombs`
* `score`
* `graze`
* `difficulty`

### 상수

* `pi`
* `tau`

\---

## 5\. 내장 함수

### 5-1. 수학 함수

* `abs(x)`
* `min(a, b, ...)`
* `max(a, b, ...)`
* `clamp(x, a, b)`
* `clamp01(x)`
* `lerp(a, b, t)`
* `sin(x)`
* `cos(x)`
* `tan(x)`
* `asin(x)`
* `acos(x)`
* `atan(x)`
* `atan2(y, x)`
* `sqrt(x)`
* `floor(x)`
* `ceil(x)`
* `sign(x)`

### 5-2. 탄막용 함수

* `rand()`
* `rand(a)`
* `rand(a, b)` (아직은 안됨으로 넣었다간 숫자가 밀려 참사가 남으로 숫자 하나만 넣자.)
* `dist(x1, y1, x2, y2)`
* `angle(x1, y1, x2, y2)`

### 5-3. 함수 주의점

* `sin`, `cos`, `tan`, `atan`, `atan2`, `asin`, `acos`는 **라디안 기준**이다.
* `angle(...)`은 **도 단위** 값을 돌려준다.
* `rand(a, b)`는 두 값 사이의 난수를 만든다.
* `dist(...)`는 두 점 사이 거리다.

\---

## 6\. 색상 표기

색상은 여러 방식으로 쓸 수 있다.

### 6-1. 16진수

```txt
FF0000
00FF00
0000FF
```

### 6-2. `#` 접두사

```txt
#FFAA00
```

### 6-3. `0x` 접두사

```txt
0xFFAA00
```

### 6-4. RGB 콤마 표기

```txt
255,128,0
```

색상은 기본적으로 `RRGGBB`로 해석된다.  
`red`, `blue` 같은 이름형 색상은 지원하지 않는다.

\---

## 7\. 스테이지 파일 문법

스테이지 파일은 보통 이런 식이다.

```txt
bg 10,10,25
player 480 620
power 0

enemy 200 100 20 14 stages/stage1/enemy1.txt stages/stage1/fairy.png
enemy 760 100 20 14 stages/stage1/enemy1.txt stages/stage1/fairy.png
```

### 7-1. `bg`

배경색을 정한다.

```txt
bg <color>
```

예:

```txt
bg 10,10,25
bg #101019
```

## 7-2. 배경 이미지 설정

스테이지 배경은 `stages/stageN/stage.txt` 안에서 설정한다.

### 형식

```txt
bgimg <이미지경로>
````

또는 아래 이름도 사용할 수 있다.

```txt
bgimage <이미지경로>
background <이미지경로>
```

### 지원 형식

* `.bmp`
* `.png`
* `.jpg`
* `.jpeg`

### 경로 규칙

* **상대경로만 사용**
* `..` 사용 불가
* `:` 사용 불가
* `\\` 대신 `/` 사용 권장
* 아래 폴더 안의 경로만 허용됨

  * `stages/`
  * `ply/`
  * `scripts/`
  * `assets/`

### 배경색 함께 설정

배경 이미지를 쓰더라도 기본 배경색을 함께 지정할 수 있다.

```txt
bg 101828
```

### 예시

```txt
# 스테이지 배경
bg 101828
bgimg assets/backgrounds/stage1.png
```

```txt
# BMP 배경
bg 1A1A2A
background assets/bg/stage2.bmp
```

### 주의

* 배경 이미지는 화면 전체에 **타일 방식**으로 반복 표시된다.
* 이미지 로딩에 실패하면 배경색(`bg`)만 적용된다.
* 파일 크기나 경로가 너무 크거나 잘못되면 읽지 못할 수 있다.



### 7-3. `player`

플레이어 시작 위치를 정한다.

```txt
player <x> <y>
```

### 7-4. `power`

플레이어 시작 파워 레벨을 정한다.

```txt
power <n>
```

### 7-5. `enemy`

적 하나를 배치한다.

```txt
enemy <x> <y> <hp> <radius> <scriptPath> \[spritePath] \[color]
```

예:

```txt
enemy 200 100 20 14 stages/stage1/enemy1.txt stages/stage1/fairy.png
```

#### 항목 설명

* `<x> <y>`: 시작 위치
* `<hp>`: 체력
* `<radius>`: 충돌 반경
* `<scriptPath>`: 적 스크립트 파일
* `\[spritePath]`: 적 그림 파일
* `\[color]`: 기본 색상

\---

## 8\. 파워 프로필 파일 문법

예:

```txt
name Normal
player\_sprite assets/player.png
bomb\_sprite assets/bomb.png
shot\_sprite assets/shot.png
bomb\_shape circle
shot\_color FFDD88
bomb\_color 66DDEE
fire\_cooldown 6
shot\_count 2
shot\_speed 12
shot\_spread\_deg 10
shot\_damage 1
hit\_radius 4
graze\_radius 16
bomb\_radius 96
bomb\_duration 60
bomb\_invuln 120
bomb\_clear\_radius 120
```

### 8-1. 지원 키

* `name`
* `player\_sprite`
* `bomb\_sprite`
* `shot\_sprite`
* `bomb\_shape`
* `shot\_color`
* `bomb\_color`
* `fire\_cooldown`
* `shot\_count`
* `shot\_speed`
* `shot\_spread\_deg`
* `shot\_damage`
* `hit\_radius`
* `graze\_radius`
* `bomb\_radius`
* `bomb\_duration`
* `bomb\_invuln`
* `bomb\_clear\_radius`

### 8-2. `bomb\_shape`

지원 값:

* `ring`
* `circle`
* `diamond`
* `cross`

\---

## 9\. 적 스크립트 문법

적 스크립트는 한 줄에 한 명령이다.

### 기본 예시

```txt
wait 60
shot aimed 3.0
wait 60
shot circle 12 2.5 FF6644
wait 90
end
```

\---

## 10\. 시간 / 대기 명령

### 10-1. `wait`

정해진 틱 수만큼 기다린다.

```txt
wait <expr>
```

예:

```txt
wait 60
wait 30 + 30
```

### 10-2. `waitrand`

두 값 사이에서 랜덤 대기한다.

```txt
waitrand <min> <max>
```

예:

```txt
waitrand 30 90
```

\---

## 11\. 변수 설정 명령

### 11-1. `set`

변수에 값을 넣는다.

```txt
set <var> <expr>
```

### 11-2. `add`

변수에 더한다.

```txt
add <var> <expr>
```

### 11-3. `sub`

변수에서 뺀다.

```txt
sub <var> <expr>
```

### 11-4. `mul`

변수에 곱한다.

```txt
mul <var> <expr>
```

### 11-5. `div`

변수를 나눈다.

```txt
div <var> <expr>
```

### 11-6. `randset`

변수에 랜덤 값을 넣는다.

```txt
randset <var> <min> <max>
```

예:

```txt
randset angle -30 30
```

\---

## 12\. 조건 / 분기

### 12-1. `if`

조건이 참일 때만 아래 블록을 실행한다.

```txt
if <expr>
    ...
endif
```

예:

```txt
if hp < 10
    shot aimed 4.0
endif
```

### 12-2. `endif`

`if` 블록을 닫는다.

```txt
endif
```

`else`, `elseif`는 없다.

\---

## 13\. 반복

### 13-1. `loop`

지정한 횟수만큼 반복한다.

```txt
loop <count>
    ...
endloop
```

예:

```txt
loop 5
    shot circle 12 2.2
    wait 20
endloop
```

### 13-2. 무한 반복

`count`가 음수면 무한 반복으로 취급된다.

```txt
loop -1
    shot aimed 3.0
    wait 60
endloop
```

### 13-3. `endloop`

`loop` 블록을 닫는다.

```txt
endloop
```

### 13-4. `for`

숫자 범위를 직접 돈다.

```txt
for <var> <start> <end> \[step]
    ...
endfor
```

예:

```txt
for i 0 7 1
    shot aimed 2.5 offset i \* 10
endfor
```

#### 설명

* `<var>`: 루프 변수
* `<start>`: 시작값
* `<end>`: 끝값
* `\[step]`: 증가량, 생략 시 1로 본다

`step`이 양수면 `start <= end`일 때 실행되고,  
음수면 `start >= end`일 때 실행된다.

### 13-5. `endfor`

`for` 블록을 닫는다.

```txt
endfor
```

\---

## 14\. 흐름 제어

### 14-1. `label`

이름표를 만든다.

```txt
label start
```

### 14-2. `goto`

라벨로 이동한다.

```txt
goto start
```

### 14-3. `call`

라벨을 호출한다. 돌아올 위치를 스택에 저장한다.

```txt
call patternA
```

### 14-4. `return`

`call`에서 돌아온다.

```txt
return
```

### 14-5. `break`

가장 안쪽 반복문을 끝낸다.

```txt
break
```

### 14-6. `continue`

현재 반복문의 다음 회차로 간다.

```txt
continue
```

`for`에서는 루프 변수를 한 번 더 진행한 뒤 이어서 검사한다.

### 14-7. `jump`

명령 인덱스로 직접 이동한다.

```txt
jump <expr>
```

주의:

* 주석 제거 후 남은 **명령 목록 인덱스**를 기준으로 이동한다
* 잘못된 인덱스는 무시된다

### 14-8. `end`

스크립트를 끝낸다.

```txt
end
```

\---

## 15\. 이동 명령

### 15-1. `move`

적을 지정 위치로 이동시킨다.

```txt
move <x> <y> <speed>
```

예:

```txt
move 480 160 4
```

### 15-2. `move auto`

플레이어 상태를 참고해서 유리한 위치로 자동 이동한다.

```txt
move auto \[speed] \[distance]
```

예:

```txt
move auto 3 220
```

기본값:

* speed 생략 시 `2`
* distance 생략 시 `220`

\---

## 16\. 탄 발사 명령

### 16-1. `shot circle`

원형으로 탄을 발사한다.

```txt
shot circle <count> <speed> \[color]
shot ring <count> <speed> \[color]
shot burst <count> <speed> \[color]
```

예:

```txt
shot circle 16 3 FF6644
```

### 16-2. `shot fan`

플레이어 방향을 기준으로 부채꼴 탄을 발사한다.

```txt
shot fan <count> <speed> <spreadDeg> \[offset <deg>] \[color <value>]
```

짧게 쓰면 다음도 가능하다.

```txt
shot fan 7 3.0 8
shot fan 7 3.0 8 offset 15
shot fan 7 3.0 8 color FF6655
shot fan 7 3.0 8 offset 15 color 255,120,80
```

#### 의미

* `<count>`: 탄 개수
* `<speed>`: 탄 속도
* `<spreadDeg>`: 탄 사이 각도 간격
* `offset <deg>`: 플레이어 방향에서 얼마나 비틀지
* `color`: 색상

### 16-3. `shot aimed`

플레이어를 향해 탄을 쏜다.

```txt
shot aimed <speed> \[offset <deg>] \[color <value>]
```

예:

```txt
shot aimed 3.2
shot aimed 3.2 offset 10
shot aimed 3.2 color FF5566
shot aimed 3.2 offset -15 color 255,120,80
```

### 16-4. `shot spiral`

나선형 탄막을 만든다.

```txt
shot spiral <count> <speed> <phaseStep> \[color]
```

예:

```txt
shot spiral 12 1.6 0.18 FF6644
```

\---

## 17\. 레이저 발사 명령

### 17-1. 일반 레이저

```txt
laser <angleDeg> <growSpeed> <maxLength> <duration> <width> \[color]
```

예:

```txt
laser 90 8 800 120 6 66FF66
```

#### 의미

* `<angleDeg>`: 발사 방향 각도
* `<growSpeed>`: 레이저가 늘어나는 속도
* `<maxLength>`: 최대 길이
* `<duration>`: 유지 시간
* `<width>`: 두께
* `\[color]`: 색상

### 17-2. 조준 레이저

```txt
laser aimed <growSpeed> <maxLength> <duration> <width> \[offset <deg>] \[color <value>]
```

예:

```txt
laser aimed 8 1000 100 4
laser aimed 8 1000 100 4 offset 10
laser aimed 8 1000 100 4 color 66FF66
laser aimed 8 1000 100 4 offset -8 color 120,255,120
```

#### 의미

* 플레이어 방향을 향해 레이저를 쏜다
* `offset`으로 조준각을 비틀 수 있다

\---

## 18\. 명령 조합 예시

### 18-1. 기본 보스 패턴

```txt
label intro
wait 60
shot aimed 3.0
wait 30
shot aimed 3.0 offset 8
wait 30
shot aimed 3.0 offset -8
wait 60
goto intro
```

### 18-2. 원형 + 나선 혼합

```txt
loop 6
    shot circle 12 2.4 FF6644
    wait 40
    shot spiral 8 1.8 0.22 FFAA55
    wait 30
endloop
end
```

### 18-3. 조건 분기

```txt
if hp < 10
    shot fan 9 3.2 6 offset 5 color FF5555
endif
```

현재는 `else`를 직접 지원하지 않으므로, `if`와 `endif`만 써야 한다.

### 18-4. for 반복

```txt
for i 0 5 1
    shot aimed 2.8 offset i \* 6
    wait 10
endfor
```

\---

## 19\. 현재 코드 기준 주의점

### 19-1. `else`는 없다

`if ... endif`만 있다.

### 19-2. 문자열 변수는 없다

숫자 변수만 있다.

### 19-3. 배열은 없다

`a\[0]` 같은 문법은 없다.

### 19-4. 함수 정의는 없다

`label`과 `call`은 있지만, 사용자 정의 함수 문법은 따로 없다.

### 19-5. 줄 번호 점프가 아니다

`jump 10`은 파일의 10번째 줄이 아니라, 파싱된 명령 목록의 인덱스를 뜻한다.

\---

## 20\. 초보자용 안전 규칙

탄막 스크립트를 처음 만들 때는 다음을 지키는 편이 좋다.

* `loop -1`은 무한 반복이므로 반드시 탈출 경로를 생각한다
* `wait` 없이 무한 반복하지 않는다
* `shot circle 1000 ...`처럼 과도한 수는 피한다
* `for`의 시작/끝/step을 분명히 쓴다
* `laser`의 `width`는 1 이상으로 둔다
* `goto`와 `call`은 라벨 이름을 정확히 맞춘다

\---

## 21\. 빠른 참고표

### 기본 명령

|명령|형태|
|-|-|
|wait|`wait <expr>`|
|waitrand|`waitrand <min> <max>`|
|set|`set <var> <expr>`|
|add|`add <var> <expr>`|
|sub|`sub <var> <expr>`|
|mul|`mul <var> <expr>`|
|div|`div <var> <expr>`|
|randset|`randset <var> <min> <max>`|
|if|`if <expr>` / `endif`|
|loop|`loop <count>` / `endloop`|
|for|`for <var> <start> <end> \[step]` / `endfor`|
|label|`label <name>`|
|goto|`goto <name>`|
|call|`call <name>`|
|return|`return`|
|break|`break`|
|continue|`continue`|
|jump|`jump <expr>`|
|end|`end`|

### 공격 명령

|명령|형태|
|-|-|
|shot circle|`shot circle <count> <speed> \[color]`|
|shot ring|`shot ring <count> <speed> \[color]`|
|shot burst|`shot burst <count> <speed> \[color]`|
|shot fan|`shot fan <count> <speed> <spreadDeg> \[offset <deg>] \[color <value>]`|
|shot aimed|`shot aimed <speed> \[offset <deg>] \[color <value>]`|
|shot spiral|`shot spiral <count> <speed> <phaseStep> \[color]`|
|laser|`laser <angleDeg> <growSpeed> <maxLength> <duration> <width> \[color]`|
|laser aimed|`laser aimed <growSpeed> <maxLength> <duration> <width> \[offset <deg>] \[color <value>]`|

\---

## 22\. 한 줄 예시 모음

```txt
wait 60
waitrand 30 90
set hpRatio hp / maxhp
randset phaseTimer 60 120
shot aimed 3.0 offset 10 color FF6666
shot fan 9 2.5 6 offset 0 color 88CCFF
shot circle 16 2.2 66FF66
shot spiral 12 1.6 0.18 FFAA55
laser aimed 8 1000 100 4 offset -12 color 66FF66
move auto 3 220
```

\---

## 23\. 마지막 참고

이 문서의 기준은 **현재 코드에 실제로 구현된 문법**이다.  
새 명령을 추가하면 이 문서도 같이 업데이트하는 편이 좋다.

