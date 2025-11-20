# USB/IP over SSH 터널링 구현 문서

## 개요

이 문서는 Sunshine 서버에 구현된 SSH 터널링 기반 USB/IP 기능에 대한 설명입니다.

## 시스템 아키텍처

```
[클라이언트]                    [Sunshine 서버 (호스트)]
    |                                  |
    | SSH 연결 (인증)                  |
    |--------------------------------->|
    |                                  |
    | SSH 터널링                       | SSH 서버 (libssh)
    | (USB/IP 포트 포워딩)             |   - 동적 포트 할당
    |<-------------------------------->|   - 사용자 인증
    |                                  |
    | nvhttp REST API 호출             |
    | (USB 디바이스 리스트 조회)        |
    |--------------------------------->| nvhttp 엔드포인트
    |                                  |   - /usbip/devlist
    |<---------------------------------|   - /usbip/import
    |  JSON 응답 (디바이스 목록)        |   - /usbip/export
    |                                  |
    | nvhttp API 호출                  |
    | (특정 USB 디바이스 마운트 요청)   |
    |--------------------------------->|
    |                                  | USB/IP 클라이언트
    |                                  |   - localhost:forwarded_port
    |<---------------------------------|   - 디바이스 import
    |  마운트 성공                      |
    |                                  |
    | USB 통신 (USB/IP 프로토콜)       | Windows vhci-hcd
    |<================================>| (가상 USB 호스트)
```

## 주요 구성 요소

### 1. SSH 서버 (src/ssh_server.h/cpp)

**기능:**
- libssh 라이브러리를 사용한 SSH 서버 구현
- 동적 포트 할당 (기본: 2222부터 시작)
- 사용자 이름/비밀번호 인증
- SSH 역방향 포트 포워딩 지원

**주요 클래스:**
- `ssh_server_t`: SSH 서버 메인 클래스
- `tunnel_info_t`: SSH 터널 정보 구조체

**주요 메서드:**
- `start(port, username, password)`: SSH 서버 시작
- `stop()`: SSH 서버 종료
- `get_active_tunnels()`: 활성 터널 목록 조회
- `get_tunnel(client_id)`: 특정 클라이언트의 터널 정보 조회

### 2. USB/IP 클라이언트 (src/usbip_client.h/cpp)

**기능:**
- USB/IP 프로토콜 직접 구현 (외부 의존성 없음)
- USB 디바이스 목록 조회 (OP_REQ_DEVLIST/OP_REP_DEVLIST)
- USB 디바이스 import/export (OP_REQ_IMPORT/OP_REP_IMPORT)
- 여러 클라이언트 연결 관리

**주요 클래스:**
- `usbip_client_t`: USB/IP 클라이언트
- `usb_device_info_t`: USB 디바이스 정보 구조체
- `usbip_manager_t`: 여러 USB/IP 연결 관리

**주요 메서드:**
- `connect(host, port)`: USB/IP 서버 연결
- `get_device_list()`: 디바이스 목록 조회
- `import_device(busid)`: 디바이스 마운트
- `export_device(busid)`: 디바이스 언마운트

### 3. nvhttp 엔드포인트 (src/nvhttp.cpp)

**새로운 REST API 엔드포인트:**

#### GET /ssh/info
SSH 서버 연결 정보 조회 (포트, 사용자명, 비밀번호)

**응답 예시:**
```json
{
  "status": "success",
  "enabled": true,
  "port": 2222,
  "username": "sunshine",
  "password": "aB3#xY9@pL2mK7qR5t",
  "active_tunnels": 2
}
```

**참고:**
- 비밀번호는 서버가 시작될 때마다 자동으로 생성됩니다 (20자 랜덤)
- 로그에서도 비밀번호를 확인할 수 있습니다
- 보안상 이 엔드포인트는 인증된 클라이언트만 접근 가능합니다

#### GET /usbip/devlist
SSH 터널을 통해 연결된 모든 클라이언트의 USB 디바이스 목록 조회

**응답 예시:**
```json
{
  "status": "success",
  "devices": [
    {
      "client_id": "user@client1",
      "client_ip": "192.168.1.100",
      "busid": "1-1",
      "path": "/sys/devices/...",
      "busnum": 1,
      "devnum": 2,
      "idVendor": 1234,
      "idProduct": 5678,
      "manufacturer": "Example Corp",
      "product": "USB Device",
      "serial": "ABC123"
    }
  ]
}
```

#### GET /usbip/import?client_id=xxx&busid=xxx
특정 클라이언트의 USB 디바이스를 호스트 컴퓨터에 마운트

**요청 파라미터:**
- `client_id`: SSH 클라이언트 ID
- `busid`: USB 디바이스 버스 ID (예: "1-1")

**응답 예시:**
```json
{
  "status": "success",
  "message": "Device imported successfully",
  "busid": "1-1"
}
```

#### GET /usbip/export?busid=xxx
마운트된 USB 디바이스를 언마운트

**요청 파라미터:**
- `busid`: 언마운트할 USB 디바이스 버스 ID

### 4. 설정 (src/config.h/cpp)

**SSH 서버 설정 (`config::ssh_server_t`):**
```cpp
bool enabled;               // SSH 서버 활성화 여부
int port;                   // 포트 (0 = 동적 할당)
std::string username;       // 인증 사용자명
std::string password;       // 인증 비밀번호
std::string host_key_file;  // 호스트 키 파일 경로
```

**USB/IP 설정 (`config::usbip_t`):**
```cpp
bool enabled;                              // USB/IP 활성화 여부
int default_port;                          // 기본 USB/IP 포트 (3240)
bool auto_import;                          // 자동 import 여부
std::vector<std::string> allowed_devices;  // 허용 디바이스 패턴
```

**기본값:**
- SSH 서버: 비활성화, 포트 2222, username/password: "sunshine"
- USB/IP: 비활성화, 포트 3240, 자동 import 비활성화

## 빌드 요구사항

### Windows (MinGW UCRT64)
- **libssh**: SSH 서버 기능
  ```bash
  pacman -S mingw-w64-ucrt-x86_64-libssh
  ```

자세한 의존성 설치 방법은 `updater/readme.md`를 참조하세요.

### 선택적 의존성
libssh가 설치되지 않은 경우, SSH 서버 기능이 비활성화되고 `DISABLE_SSH_SERVER` 매크로가 정의됩니다.

## 사용 흐름

### 1. 서버 시작
Sunshine 서버가 시작되면 자동으로:
- SSH 서버 초기화 및 시작 (config에서 enabled인 경우)
- USB/IP 클라이언트 매니저 초기화

### 2. SSH 연결 정보 확인
먼저 SSH 연결 정보를 조회:
```
GET https://sunshine-host:47984/ssh/info
```

응답에서 `port`, `username`, `password`를 확인합니다.

### 3. 클라이언트 연결
1. 클라이언트가 조회한 정보로 SSH 연결
   ```bash
   ssh -R 3240:localhost:3240 username@sunshine-host -p port
   ```
2. 랜덤 생성된 비밀번호로 인증
3. SSH 역방향 포트 포워딩이 자동으로 설정됨
   - 클라이언트의 USB/IP 서버 포트(3240)를 Sunshine 서버로 터널링

### 4. 디바이스 조회
클라이언트(또는 Web UI)가 nvhttp API를 통해 디바이스 목록 조회:
```
GET https://sunshine-host:47984/usbip/devlist
```

### 5. 디바이스 마운트
원하는 USB 디바이스를 호스트에 마운트:
```
GET https://sunshine-host:47984/usbip/import?client_id=user@client&busid=1-1
```

### 6. USB 통신
마운트된 USB 디바이스는 호스트 컴퓨터에서 물리적으로 연결된 것처럼 동작

### 7. 디바이스 언마운트
사용 완료 후:
```
GET https://sunshine-host:47984/usbip/export?busid=1-1
```

## 보안 고려사항

1. **SSH 인증**
   - 서버 시작 시 20자 랜덤 비밀번호 자동 생성
   - 매번 재시작할 때마다 새로운 비밀번호 생성
   - `/ssh/info` API를 통해서만 비밀번호 확인 가능
   - 프로덕션 환경에서는 공개키 인증 추가 권장

2. **방화벽 설정**
   - SSH 포트 (기본 2222) 방화벽 규칙 설정
   - nvhttp 엔드포인트는 기존 Sunshine 인증 메커니즘 사용

3. **디바이스 접근 제어**
   - `allowed_devices` 설정으로 특정 디바이스만 허용 가능
   - 화이트리스트 기반 접근 제어

## 제한사항

1. **Windows 전용**
   - 현재 구현은 Windows용으로 작성됨
   - Linux/macOS 지원을 위해서는 추가 작업 필요

2. **USB/IP 프로토콜**
   - 기본 USB/IP 프로토콜만 구현
   - 일부 고급 기능은 미지원

3. **동시 연결**
   - 여러 클라이언트의 동시 SSH 연결 지원
   - 각 클라이언트는 독립적인 USB/IP 연결 유지

## 향후 개선 사항

1. **공개키 인증 지원**
   - 비밀번호 대신 SSH 키 기반 인증

2. **자동 디바이스 발견**
   - 클라이언트 연결 시 자동으로 디바이스 목록 업데이트

3. **Web UI 통합**
   - Sunshine Web UI에서 USB 디바이스 관리 인터페이스 추가

4. **디바이스 필터링**
   - 디바이스 클래스, VID/PID 기반 필터링

5. **로깅 및 모니터링**
   - USB 디바이스 연결/해제 이벤트 로깅
   - 디바이스 사용 통계

## 문제 해결

### SSH 서버가 시작되지 않음
- libssh 설치 확인
- 포트 충돌 확인 (netstat -an | findstr :2222)
- 로그 파일 확인

### USB 디바이스가 보이지 않음
- SSH 터널이 정상적으로 설정되었는지 확인
- 클라이언트에서 USB/IP 서버가 실행 중인지 확인
- /usbip/devlist 엔드포인트 직접 호출하여 응답 확인

### 디바이스 import 실패
- Windows vhci-hcd 드라이버 설치 확인
- 관리자 권한으로 Sunshine 실행 확인
- 디바이스가 이미 다른 곳에서 사용 중인지 확인

## 파일 목록

**새로 추가된 파일:**
- `src/ssh_server.h`: SSH 서버 헤더
- `src/ssh_server.cpp`: SSH 서버 구현
- `src/usbip_client.h`: USB/IP 클라이언트 헤더
- `src/usbip_client.cpp`: USB/IP 클라이언트 구현
- `USB_IP_IMPLEMENTATION.md`: 이 문서

**수정된 파일:**
- `src/config.h`: SSH/USB/IP 설정 구조체 추가
- `src/config.cpp`: 설정 초기화
- `src/main.cpp`: SSH 서버 및 USB/IP 클라이언트 초기화
- `src/nvhttp.cpp`: USB/IP REST API 엔드포인트 추가
- `cmake/compile_definitions/common.cmake`: 소스 파일 추가
- `cmake/dependencies/windows.cmake`: libssh 의존성 추가

## 라이센스

이 구현은 Sunshine 프로젝트의 라이센스를 따릅니다.
