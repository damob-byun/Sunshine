# SSH & USB/IP API 문서

## 개요

Sunshine 서버에 SSH 터널링 기반 USB/IP 원격 USB 디바이스 전달 기능을 위한 REST API입니다.

## 아키텍처

```
클라이언트                     Sunshine 서버
    |                              |
    | 1. GET /ssh/info            |
    |<--------------------------->| SSH 연결 정보 제공
    |                              | (port, username, password)
    |                              |
    | 2. SSH 연결 + 포트포워딩    |
    |<===========================>| SSH 서버 (libssh)
    | ssh -R 3240:localhost:3240  | 터널 생성 및 관리
    |                              |
    | 3. GET /usbip/devlist       |
    |<--------------------------->| SSH 터널 통해
    |                              | USB 디바이스 목록 조회
    |                              |
    | 4. GET /usbip/import        |
    |<--------------------------->| USB/IP 클라이언트로
    |                              | 디바이스 마운트
    |                              |
    | 5. USB 통신                  |
    |<===========================>| 호스트에 USB 디바이스
    |    (USB/IP 프로토콜)         | 마운트됨
```

---

## API 엔드포인트

### 1. SSH 서버 정보 조회

#### `GET /ssh/info`

SSH 서버의 연결 정보를 조회합니다.

**요청:**
```http
GET https://sunshine-server:47984/ssh/info
```

**응답 (성공):**
```json
{
  "status": "success",
  "enabled": true,
  "port": 2222,
  "username": "sunshine",
  "password": "aB3#xY9@pL2mK7qR5t",
  "active_tunnels": 0
}
```

**응답 (SSH 서버 비활성화):**
```json
{
  "status": "error",
  "message": "SSH server is not running",
  "enabled": false
}
```

**필드 설명:**
- `status`: 요청 처리 상태 (`"success"` 또는 `"error"`)
- `enabled`: SSH 서버 활성화 여부
- `port`: SSH 서버 포트 번호
- `username`: SSH 인증 사용자명
- `password`: 자동 생성된 SSH 비밀번호 (20자 랜덤)
- `active_tunnels`: 현재 활성화된 SSH 터널 개수

**참고:**
- 비밀번호는 서버가 시작될 때마다 새로 생성됩니다
- 서버 로그에서도 비밀번호를 확인할 수 있습니다
- 이 API는 인증된 클라이언트만 접근 가능합니다

---

### 2. USB 디바이스 목록 조회

#### `GET /usbip/devlist`

SSH 터널로 연결된 모든 클라이언트의 USB 디바이스 목록을 조회합니다.

**전제 조건:**
- SSH 터널이 설정되어 있어야 함
- 클라이언트에서 USB/IP 서버가 실행 중이어야 함

**요청:**
```http
GET https://sunshine-server:47984/usbip/devlist
```

**응답 (성공):**
```json
{
  "status": "success",
  "devices": [
    {
      "client_id": "sunshine@192.168.1.100",
      "client_ip": "192.168.1.100",
      "busid": "1-1",
      "path": "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1",
      "busnum": 1,
      "devnum": 2,
      "speed": 480,
      "idVendor": 1234,
      "idProduct": 5678,
      "bDeviceClass": 0,
      "manufacturer": "Example Corp",
      "product": "USB Mouse",
      "serial": "ABC123456"
    },
    {
      "client_id": "sunshine@192.168.1.101",
      "client_ip": "192.168.1.101",
      "busid": "2-1",
      "path": "/sys/devices/pci0000:00/0000:00:14.0/usb2/2-1",
      "busnum": 2,
      "devnum": 3,
      "speed": 12,
      "idVendor": 8765,
      "idProduct": 4321,
      "bDeviceClass": 3,
      "manufacturer": "Another Corp",
      "product": "USB Keyboard",
      "serial": "XYZ789012"
    }
  ]
}
```

**응답 (SSH 터널 없음):**
```json
{
  "status": "success",
  "message": "No active SSH tunnels",
  "devices": []
}
```

**응답 (오류):**
```json
{
  "status": "error",
  "message": "SSH server is not running"
}
```

**필드 설명:**
- `client_id`: SSH 클라이언트 식별자
- `client_ip`: 클라이언트 IP 주소
- `busid`: USB 버스 ID (디바이스 고유 식별자)
- `path`: 시스템 디바이스 경로
- `busnum`: USB 버스 번호
- `devnum`: USB 디바이스 번호
- `speed`: USB 속도 (Mbps)
  - `12`: USB 1.1 (Low/Full Speed)
  - `480`: USB 2.0 (High Speed)
  - `5000`: USB 3.0 (SuperSpeed)
- `idVendor`: USB Vendor ID (16진수)
- `idProduct`: USB Product ID (16진수)
- `bDeviceClass`: USB 디바이스 클래스
  - `0`: Device
  - `3`: HID (Human Interface Device)
  - `7`: Printer
  - `8`: Mass Storage
  - `9`: Hub
- `manufacturer`: 제조사 이름
- `product`: 제품 이름
- `serial`: 시리얼 번호

---

### 3. USB 디바이스 마운트

#### `GET /usbip/import`

특정 클라이언트의 USB 디바이스를 Sunshine 서버(호스트)에 마운트합니다.

**쿼리 파라미터:**
- `client_id` (required): SSH 클라이언트 ID
- `busid` (required): 마운트할 USB 디바이스의 버스 ID

**요청 예시:**
```http
GET https://sunshine-server:47984/usbip/import?client_id=sunshine@192.168.1.100&busid=1-1
```

**응답 (성공):**
```json
{
  "status": "success",
  "message": "Device imported successfully",
  "busid": "1-1"
}
```

**응답 (오류 - 터널 없음):**
```json
{
  "status": "error",
  "message": "Client tunnel not found"
}
```

**응답 (오류 - 디바이스 import 실패):**
```json
{
  "status": "error",
  "message": "Failed to import device"
}
```

**참고:**
- 마운트된 USB 디바이스는 호스트 컴퓨터에서 로컬 USB처럼 인식됩니다
- Windows에서는 vhci-hcd 드라이버가 필요합니다
- 동시에 여러 디바이스를 마운트할 수 있습니다

---

### 4. USB 디바이스 언마운트

#### `GET /usbip/export`

호스트에 마운트된 USB 디바이스를 언마운트합니다.

**쿼리 파라미터:**
- `busid` (required): 언마운트할 USB 디바이스의 버스 ID

**요청 예시:**
```http
GET https://sunshine-server:47984/usbip/export?busid=1-1
```

**응답 (성공):**
```json
{
  "status": "success",
  "message": "Device exported successfully",
  "busid": "1-1"
}
```

**응답 (오류 - 디바이스 없음):**
```json
{
  "status": "error",
  "message": "Device not found or not imported"
}
```

---

## 사용 시나리오

### 시나리오 1: 원격 USB 마우스 사용하기

```bash
# 1. SSH 정보 확인
curl https://sunshine-server:47984/ssh/info

# 응답: {"status":"success","port":2222,"username":"sunshine","password":"aB3#..."}

# 2. 클라이언트에서 SSH 터널 생성
ssh -R 3240:localhost:3240 sunshine@sunshine-server -p 2222
# 비밀번호 입력: aB3#...

# 3. 사용 가능한 USB 디바이스 확인
curl https://sunshine-server:47984/usbip/devlist

# 응답: {"status":"success","devices":[{"busid":"1-1","product":"USB Mouse",...}]}

# 4. USB 마우스 마운트
curl "https://sunshine-server:47984/usbip/import?client_id=sunshine@192.168.1.100&busid=1-1"

# 이제 호스트 컴퓨터에서 USB 마우스 사용 가능!

# 5. 사용 완료 후 언마운트
curl "https://sunshine-server:47984/usbip/export?busid=1-1"
```

### 시나리오 2: 여러 클라이언트의 USB 디바이스 관리

```bash
# 클라이언트 A (192.168.1.100) - USB 키보드 공유
ssh -R 3240:localhost:3240 sunshine@sunshine-server -p 2222

# 클라이언트 B (192.168.1.101) - USB 프린터 공유
ssh -R 3240:localhost:3240 sunshine@sunshine-server -p 2222

# 서버에서 모든 디바이스 확인
curl https://sunshine-server:47984/usbip/devlist
# 두 클라이언트의 모든 USB 디바이스가 리스트에 나타남

# 클라이언트 A의 키보드 마운트
curl "https://sunshine-server:47984/usbip/import?client_id=sunshine@192.168.1.100&busid=1-1"

# 클라이언트 B의 프린터 마운트
curl "https://sunshine-server:47984/usbip/import?client_id=sunshine@192.168.1.101&busid=2-1"
```

---

## 오류 처리

### 일반적인 오류 응답 형식

```json
{
  "status": "error",
  "message": "오류 설명 메시지"
}
```

### 주요 오류 케이스

| 오류 메시지 | 원인 | 해결 방법 |
|-----------|------|----------|
| `SSH server is not running` | SSH 서버가 실행되지 않음 | config에서 `ssh_server.enabled = true` 설정 |
| `No active SSH tunnels` | SSH 터널이 없음 | 클라이언트에서 SSH 연결 먼저 수행 |
| `Client tunnel not found` | 지정한 client_id가 존재하지 않음 | `/usbip/devlist`로 올바른 client_id 확인 |
| `Failed to connect to USB/IP server` | 클라이언트의 USB/IP 서버 미실행 | 클라이언트에서 USB/IP 서버 실행 확인 |
| `Failed to import device` | 디바이스 마운트 실패 | 디바이스 사용 중이거나 권한 문제 확인 |
| `Device not found or not imported` | 언마운트할 디바이스 없음 | 올바른 busid인지 확인 |

---

## 인증 및 보안

### 인증 방식
- 모든 API는 Sunshine의 기본 인증 메커니즘을 따릅니다
- IP 화이트리스트 기반 접근 제어 적용
- Authorization 헤더를 통한 인증 지원

### SSH 보안
- **비밀번호 자동 생성**: 20자 길이의 강력한 랜덤 비밀번호
  - 영문 대소문자, 숫자, 특수문자 조합
  - 서버 재시작 시마다 새로 생성
- **비밀번호 확인 방법**:
  1. `/ssh/info` API 호출
  2. 서버 로그 확인
- **포트 설정**: 동적 포트 할당 (기본: 2222부터 시작)

### 추천 보안 설정
1. 방화벽에서 SSH 포트만 허용
2. IP 화이트리스트 설정
3. 프로덕션 환경에서는 공개키 인증 추가 고려
4. HTTPS 사용 (Sunshine 기본 설정)

---

## 설정 파일

### SSH 서버 설정 (`config::ssh_server_t`)

```cpp
{
  "enabled": false,          // SSH 서버 활성화 여부
  "port": 0,                 // 포트 (0 = 동적 할당)
  "username": "sunshine",    // SSH 사용자명
  "password": "",            // 비어있으면 자동 생성
  "host_key_file": ""        // SSH 호스트 키 파일 (선택)
}
```

### USB/IP 설정 (`config::usbip_t`)

```cpp
{
  "enabled": false,                  // USB/IP 기능 활성화
  "default_port": 3240,              // USB/IP 서버 포트
  "auto_import": false,              // 자동 import 여부
  "allowed_devices": []              // 허용 디바이스 패턴 (빈 배열 = 모두 허용)
}
```

---

## 기술 스택

### 서버 (Sunshine)
- **libssh**: SSH 서버 구현
- **C++ STL**: 랜덤 비밀번호 생성 (`<random>`)
- **Boost.Asio**: 네트워크 통신
- **Simple-Web-Server**: HTTP/HTTPS 서버

### 프로토콜
- **SSH**: 암호화된 터널링
- **USB/IP**: USB over IP 프로토콜 (직접 구현)
- **HTTPS**: REST API 전송

---

## 빌드 및 설치

### Windows (MinGW UCRT64)

```bash
# 의존성 설치
pacman -S mingw-w64-ucrt-x86_64-libssh

# 빌드
mkdir build
cmake -B build -G Ninja -S .
ninja -C build
```

자세한 내용은 `updater/readme.md`를 참조하세요.

---

## 문제 해결

### Q: SSH 서버가 시작되지 않습니다
**A:**
1. libssh 설치 확인: `pacman -Qs libssh`
2. 포트 충돌 확인: `netstat -an | findstr :2222`
3. 로그 파일 확인: `sunshine.log`

### Q: USB 디바이스 목록이 비어있습니다
**A:**
1. SSH 터널이 연결되었는지 확인
2. 클라이언트에서 USB/IP 서버 실행 확인: `usbipd`
3. `/ssh/info`로 활성 터널 수 확인

### Q: 디바이스 import가 실패합니다
**A:**
1. Windows vhci-hcd 드라이버 설치 확인
2. 관리자 권한으로 Sunshine 실행
3. 디바이스가 다른 곳에서 사용 중인지 확인
4. busid가 정확한지 확인

### Q: 비밀번호를 잊어버렸습니다
**A:**
1. `/ssh/info` API 호출로 확인
2. 서버 로그 확인
3. 서버 재시작 (새 비밀번호 생성됨)

---

## 제한사항

1. **플랫폼**: 현재 Windows 전용으로 구현됨
2. **동시 연결**: 다중 클라이언트 지원하지만 많은 디바이스 연결 시 성능 저하 가능
3. **USB 버전**: USB 1.1, 2.0, 3.0 지원 (4.0은 미지원)
4. **디바이스 타입**: 대부분의 USB 디바이스 지원하지만 일부 특수 디바이스는 동작 안 될 수 있음

---

## 라이센스

이 구현은 Sunshine 프로젝트의 GPL-3.0 라이센스를 따릅니다.

---

## 참고 문서

- 상세 구현 문서: `USB_IP_IMPLEMENTATION.md`
- 빌드 가이드: `updater/readme.md`
- Sunshine 공식 문서: https://github.com/LizardByte/Sunshine
