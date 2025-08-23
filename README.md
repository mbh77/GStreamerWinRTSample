# GStreamerWinRTSample
이 샘플은 Direct3D11 + GStreamer 스트리밍 재생을 중심으로 구현했습니다.
UI에 렌더링 부분은 SwapChainPanel 기반의 D3D11 렌더링을 사용합니다.
또한, 애플리케이션 구조를 모듈화하고 의존성 주입(DI)을 지원하기 위해 CommunityToolkit.Mvvm과 Microsoft.Extensions.DependencyInjection을 얹은 경량 MVVM 모듈화 코드를 포함합니다.
CaptureStreamingServiceSample과 연동하여 저지연 스트리밍 동작을 확인할 수 있습니다.

### 특징
1. SwapChainPanel 렌더링: DXGI SwapChain으로 GPU 프레임 저지연 표시
2. GPU 직접 사용 (D3D11): d3d11h264dec → d3d11convert/upload → D3D11Texture2D
3. 경량 MVVM 모듈화 지원
  1. WuiMvvmApp이 DI 컨테이너(ServiceCollection → ServiceProvider)를 초기화
  2. Module 인터페이스 기반으로 서비스 등록(RegisterTypes)과 초기화(OnInitialized)를 각 모듈에 위임
  3. Ioc.Default.ConfigureServices(ServiceProvider)를 통해 CommunityToolkit.Mvvm과 연결
  4. 확장 시 새로운 모듈만 ModuleCatalog에 추가하면 됨 → 유연한 확장성 확보

### GStreamerWinRTSample 빌드 시 Unpackaged 로 설정
WinUI3 프로그램은 기본적으로 윈도우 스토어 배포를 목적으로 하는 packaged 어플리케이션 프로젝트입니다.
컴파일 및 디버깅 실행시 unpackaged로 모드 변경 후 작업 진행 바랍니다.

### 빌드를 위한 gsteamer 라이브러리 적용하기
GStreamerWinRTSample은 영상 스트리밍을 위해 gstreamer 라이브러리를 사용합니다.
gstreamer 라이브러리의 바이너리와 코드는 용량이 커서 git에서 직접 관리하기가 어렵습니다.
이에 이미 빌드된 gstreamer 라이브러리를 에셋 형태로 저장하였습니다.
빌드를 위해 이 gstreamer 라이브러리 에셋을 다운로드하여 사용해야 합니다.  

1. 빌드를 위해 아래 경로에서 이미 빌드된 gstreamer 라이브러리를 다운로드 하세요.  
[gsteamer 라이브러리](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/gstreamer.zip)
2. 다운로드한 gsteamer 라이브러리를 적당한 위치에 압축해제 합니다.
3. 솔루션 파일을 열어 솔루션을 로드합니다.
4. MediaFramework (Desktop) C++ 프로젝트를 마우스 오른쪽 클릭 후 속성을 선택합니다.
5. C/C++ > 일반 속성에서 추가 포함 디렉토리 편집으로 들어가 아래 이미지와 같이 gsteamer 헤더들이 참조될 수 있도록 gsteamer 압축 해제 경로에 맞게 편집합니다.  
![C/C++ 일반 추가 포함 디렉터리 편집](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/c_added_directory.png)
6. 링커 > 일반 > 추가 라이브러리 디렉토리를 아래 이미지와 같이 gsteamer 압축 해제 경로에 맞게 편집합니다.  
![링커 일반 추가 라이브러리 디텍터리 편집](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/linker_added_library_directory.png)


### gsteamer msvc 빌드하기 (24년 11월 11일 현재)
이미 빌드된 gsteamer 라이브러리가 버전 변경이 필요하거나 개발자가 라이브러리를 변경하여 적용이 필요할 경우 직접 gsteamer 라이브러리를 빌드해야 합니다.
윈도우용으로 사용하기 위해서는 msvc 컴파일러용으로 빌드해야 합니다.
빌드가 완료 된 후에는 \GStreamerWinRTSample\gstreamer 경로에 dll 폴더만 복사하여 빌드 후 dll이 빌드 출력물에 포함 될 수 있도록 해야 합니다.

1. gsteamer 빌드는 공식적으로 meson을 사용하여 빌드합니다. meson 사용을 위해 python 3.12.7 을 설치합니다.
2. power shell 명령어로 python, python3 를 입력했을 때 윈도우 스토어로 넘어가는 것을 막기 위해 앱 실행 별칭 설정에서 pythone 앱 설치 관리자의 별칭을 꺼 줘야 합니다.  
![python 앱 실행 별칭 설정](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/python_alias.png)
3. pip install meson 명령을 실행하여 meson을 설치합니다.
4. pip install ninja 명령을 실행하여 ninja를 설치합니다.
5. gitlab에 있는 gsteamer 코드가 공식적인 최신 코드입니다. https://gitlab.freedesktop.org/gstreamer/gstreamer/ 에서 레파지토리 체크 아웃합니다.  
"git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git" 을 실행하여 체크 아웃을 실행합니다.
6. x64 Native Tools Command Prompt for VS 2022를 관리자 권한 실행해서 명령어 창을 엽니다.
7. 체크 아웃한 폴더로 이동 후 "meson setup builddir -Dintrospection=disabled" 를 실행하여 빌드 환경을 설정합니다. builddir 빌드가 진행되는 경로로 개발자가 임의 경로로 설정 가능합니다.
8. "ninja -C builddir" 명령을 실행하여 ninja로 빌드를 수행합니다.
9. "meson install -C builddir --destdir=output" 명령을 통해 패키지 형태로 빌드 결과물을 output 폴더에 복사합니다. outdir 경로도 개발자가 임의 경로로 설정 가능합니다.
10. power shell 명령어 입력을 통해 필터 생성 및 테스트를 위해서는 아래 그림들과 같이 환경 변수를 설정해 줘야 합니다.  
![환경변수 설정 1](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/system_variable_1.png)
![환경변수 설정 2](https://github.com/mbh77/GStreamerWinRTSample/releases/download/for-asset/system_variable_2.png) 
