#include <stdio.h>			// 표준 입출력 :wq
#include <stdlib.h>			// 표준 라이브러리
#include <unistd.h>			// 유닉스 표준
#include <string.h>			// 문자열 처리
#include <arpa/inet.h>		// 인터넷 프로토콜
#include <sys/socket.h>		// 소켓 함수
#include <netinet/in.h>		// 인터넷 주소 체계 (in_port_t)
#include <pthread.h>		// 쓰레드

#define BUF_SIZE 1024			// 채팅할 때 메시지 최대 길이
#define MAX_CLNT 256			// 최대 동시 접속자 수
#define MAX_ROOM 256			// 최대 개설 가능한 방의 갯수
#define ROOM_ID_DEFAULT		-1	// 방의 초기 ID 값(방은 리스트로 구현되고 ID를 가진다.)

typedef int BOOL;			// boolean 타입 선언(예, 아니오 판단 결과)
#define TRUE	1			// boolean 참(예)
#define FALSE	0			// boolean 거짓(아니오)

							// 함수들 선언

void * handle_serv(void * arg); // 서버 쓰레드용 함수
void * handle_clnt(void * arg);		// 클라이언트 쓰레드용 함수(함수 포인터)
void error_handling(char * msg);	// 에러 처리 함수
void sendMessageUser(char * msg, int socket);

pthread_mutex_t mutx;		// 상호배제를 위한 전역변수

							// 서버에 접속한 클라이언트 구조체
struct Client
{
	int roomId;				// 방의 번호
	int socket;				// 소켓 파일디스크립터는 고유하므로, 클라이언트 ID로 활용함
	char name[BUF_SIZE];
};
typedef struct Client Client;	// struct Client를 그냥 Client로 쓸수 있게 함

int sizeClient = 0;				// 접속중인 사용자 수(arrClient 배열의 size)
Client arrClient[MAX_CLNT];		// 접속중인 사용자 구조체들의 배열

struct Room					// 채팅방 구조체 선언
{
	int id;					// 방의 번호
	char name[BUF_SIZE];	// 방의 이름
};
typedef struct Room Room;

int sizeRoom = 0;				// arrRoom 배열의 size
Room arrRoom[MAX_ROOM];		// Room의 배열(현재 개설된 방의 배열)

int issuedId = 0;			// 발급된 ID

BOOL isEmptyRoom(int roomId);
void printHowToUse(Client * client);

// 클라이언트를 배열에 추가 - 소켓을 주면 클라이언트 구조체변수를 생성해 준다.
Client * addClient(int socket, char * nick)
{
	pthread_mutex_lock(&mutx);		// 임계영역 시작
	Client *client = &(arrClient[sizeClient++]);	// 미리 할당된 공간 획득
	client->roomId = ROOM_ID_DEFAULT;					// 아무방에도 들어있지 않음
	client->socket = socket;						// 인자로 받은 소켓 저장
	strcpy(client->name, nick);
	pthread_mutex_unlock(&mutx);	// 임계영역 끝
	return client;	// 클라이언트 구조체 변수 반환
}

// 클라이언트를 배열에서 제거 - 소켓을 주면 클라이언트를 배열에서 삭제한다.
void removeClient(int socket)
{
	pthread_mutex_lock(&mutx);		// 임계영역 시작
	int i = 0;
	for (i = 0; i<sizeClient; i++)   // 접속이 끊긴 클라이언트를 삭제한다.
	{
		if (socket == arrClient[i].socket)	// 끊긴 클라이언트를 찾았으면
		{
			while (i++<sizeClient - 1)	// 찾은 소켓뒤로 모든 소켓에 대해
				arrClient[i] = arrClient[i + 1]; // 한칸씩 앞으로 당김
			break;	// for문 탈출
		}
	}
	sizeClient--;	// 접속중인 클라이언트 수 1 감소
	pthread_mutex_unlock(&mutx);	// 임계영역 끝
}

// 드이어 메인함수 등장
int main(int argc, char *argv[])	// 인자로 포트번호 받음
{
	int serv_sock, clnt_sock;		// 소켓통신 용 서버 소켓과 임시 클라이언트 소켓
	struct sockaddr_in serv_adr, clnt_adr;	// 서버 주소, 클라이언트 주소 구조체
	int clnt_adr_sz;				// 클라이언트 주소 구조체 크기
	char nick[BUF_SIZE] = { 0 };
	pthread_t t_id;					// 클라이언트 쓰레드용 ID
	pthread_t serv_id;
	void * thread_return; 
							
	if (argc != 2) {	
		printf("Usage : %s <port>\n", argv[0]);	// 사용법을 알려준다.
		exit(1);	// 프로그램 비정상 종료
	}

	// 서버 소켓의 주소 초기화
	pthread_mutex_init(&mutx, NULL);			// 커널에서 Mutex 쓰기 위해 얻어온다.
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);	// TCP용 서버 소켓 생성

	memset(&serv_adr, 0, sizeof(serv_adr));		// 서버 주소 구조체 초기화
	serv_adr.sin_family = AF_INET;				// 인터넷 통신
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);	// 현재 IP를 이용하고
	serv_adr.sin_port = htons(atoi(argv[1]));		// 포트는 사용자가 지정한 포트 사용

													// 서버 소켓에 주소를 할당한다.
	if (bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");

	// 서버 소켓을 서버용으로 설정한다.
	if (listen(serv_sock, 5) == -1)
		error_handling("listen() error");

	while (1)	
	{
		clnt_adr_sz = sizeof(clnt_adr);	// 클라이언트 구조체의 크기를 얻고
		memset(nick, 0, sizeof(BUF_SIZE));
		// 클라이언트의 접속을 받아 들이기 위해 Block 된다.(멈춘다)
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);

		//클라이언트에게 닉네임 받는다
		read(clnt_sock, nick, sizeof(nick));

		// 클라이언트와 접속이 되면 클라이언트 소켓을 배열에 추가하고 그 주소를 얻는다.
		Client * client = addClient(clnt_sock, nick);

		// 클라이언트 구조체의 주소를 쓰레드에게 넘긴다.(포트 포함됨)
		pthread_create(&t_id, NULL, handle_clnt, (void*)client); // 쓰레드 시작	
		pthread_create(&serv_id, NULL, handle_serv, (void*)client); 

		printf("%s is connected \n", client->name);

		//pthread_join(t_id, &thread_return);
		//pthread_join(serv_id, &thread_return);

		pthread_detach(serv_id); 

		pthread_detach(t_id);	//쓰레드가 종료되면 스스로 소멸되게 함
					// 접속된 클라이언트의 IP를 화면에 찍어준다.
	}
	close(serv_sock);	
	return 0;
}

void * handle_serv(void * arg)
{
	Client * client = (Client*)arg;
	int serv_sock = client->socket;
	char srcv_msg[BUF_SIZE]="";
	int i = 0;
	
	while(1)
	{
		fgets(srcv_msg, BUF_SIZE, stdin);
		if(!strcmp(srcv_msg, "q\n") || !strcmp(srcv_msg, "Q\n"))
		{	
			for(i = 0; i < sizeClient; i++)
			{
				sendMessageUser(srcv_msg, arrClient[i].socket);
				//shutdown(serv_sock, SHUT_WR);
				close(arrClient[i].socket);
			}
				exit(0);
		}
		else
		{
		}
	}
	return NULL;
}

// 모두에게 메시지를 보내는게 아니라, 특정 사용자에게만 메시지를 보낸다.
void sendMessageUser(char * msg, int socket)   // send to a members 
{	
	int length = write(socket, msg, BUF_SIZE);
}

// 특정 방 안에 있는 모든 사람에게 메시지 보내기
void sendMessageRoom(char * msg, int roomId)   // send to the same room members 
{
	int i;
	
	pthread_mutex_lock(&mutx);		// 임계 영역 진입
	for (i = 0; i<sizeClient; i++)		// 모든 사용자들 중에서 특정 방의 사람들에게 각각 메시지전송
	{
		if (arrClient[i].roomId == roomId)	
			sendMessageUser(msg, arrClient[i].socket); 
	}
	pthread_mutex_unlock(&mutx);	// 임계 영역 끝
}

// 특정 사용자가 방에 들어가 있습니까?
BOOL isInARoom(int socket)	// yes, no로 대답할 수 있다.
{
	int i = 0;
	for (i = 0; i<sizeClient; i++)	// 클라이언트 배열에서 뒤져서 특정 사용자가 room id를 갖고있으면 방에 들어가 있는 것이다.
	{
		if (arrClient[i].socket == socket	
			&& arrClient[i].roomId != ROOM_ID_DEFAULT)	
			return TRUE;	
	}
	return FALSE;	// 아니면 방에 들어가 있지 않다.
}

// 특정 문자열에서 space 문자가 있는 곳의 index 번호를 구해준다.
int getIndexSpace(char * msg)
{
	int indexSpace = 0;
	int length = strlen(msg);
	int i = 0;
	for (i = 0; i<length; i++)
	{
		if (msg[i] == ' ')	// 공백 문자를 찾아서
		{
			indexSpace = i;
			break;
		}
	}

	if (indexSpace + 1 >= length)
	{
		return -1;
	}

	return indexSpace;	// 공백문자의 위치 반환
}

// "대기실의 메뉴"에서 선택한 메뉴를 얻어온다.(사용자 메뉴는 1자리 숫자이다.)
int getSelectedWaintingRoomMenu(char *msg)
{
	if (msg == NULL) return -1;

	int indexSpace = getIndexSpace(msg); // 사용자메시지에서 공백문자는 구분자로 활용된다.
	if (indexSpace<0)
		return 0;

	char firstByte = msg[indexSpace + 1];	// 공백문자 이후에 처음 나타나는 문자 얻기

	return firstByte - '0';	// 사용자가 선택한 메뉴에서 48을 뺀다. atoi() 써도됨
}

// "방의 메뉴" - 채팅하다가 나가고 싶을 땐 나가기 명령이 필요하다.
void getSelectedRoomMenu(char * menu, char *msg)
{
	if (msg == NULL) return;	// 예외 처리 - 이런것 안하면 프로그램 죽음

	int indexSpace = getIndexSpace(msg);	// 공백문자 위치 얻기
	if (indexSpace<0) return;	// 없으면 잘못된 패킷

	char * firstByte = &msg[indexSpace + 1];	// 공백이후의 문자열 복사
	strcpy(menu, firstByte);	// 그게 메뉴인데 4바이트 크기이다.

								// all menus have 4 byte length. remove \n
	menu[4] = 0;	// 4바이트 에서 NULL 문자 넣어 문자열 끊기
	return;
}

// 방 생성하는 함수
Room * addRoom(char * name) // 방의 이름을 지정할 수 있다.
{
	pthread_mutex_lock(&mutx);		// 임계 영역 시작
	Room *room = &(arrRoom[sizeRoom++]);	// 패턴은 클라이언트와 동일
	room->id = issuedId++;			// 방의 ID 발급 - 고유해야 함
	strcpy(room->name, name);		// 방의 이름 복사
	pthread_mutex_unlock(&mutx);	// 임계 영역 끝
	return room;	// 생성된 방 구조체 변수의 주소 반환
}

// 방 제거하기
void removeRoom(int roomId)	// 방의 번호를 주면 배열에서 찾아 삭제한다.
{
	int i = 0;

	pthread_mutex_lock(&mutx);	// 임계 영역 진입
	for (i = 0; i<sizeRoom; i++)	// 모든 방에 대해서
	{
		if (arrRoom[i].id == roomId)	// 만약에 방을 찾았으면
		{
			while (i++<sizeRoom - 1)	// 그 이후의 모든 방을
				arrRoom[i] = arrRoom[i + 1]; // 앞으로 한칸씩 당긴다.
			break;
		}
	}
	sizeRoom--;	// 개설된 방의 갯수를 하나 줄인다.
	pthread_mutex_unlock(&mutx);	// 임계 영역 끝
}

// 특정 방이 존재 합니까?
BOOL isExistRoom(int roomId)	// 방의 번호를 주고, 만들어진 방인지 확인한다.
{
	int i = 0;
	for (i = 0; i<sizeRoom; i++)		// 모든 방에 대해서
	{
		if (arrRoom[i].id == roomId)	// 만약에 특정 방을 찾았으면
			return TRUE;	// 참을 반환한다.
	}
	return FALSE;	// 다 뒤졌는데 못찾으면 거짓을 반환한다.
}

// 사용자가 특정 방에 들어가기
void enterRoom(Client * client, int roomId) // 클라이언트가 roomID의 방에 들어간다.
{
	char her[BUF_SIZE] = "**Commands in ChatRoom**\n exit : Exit Room \n list : User in this room \n help : This message \n";
	char buf[BUF_SIZE] = "";
	if (!isExistRoom(roomId))	// 방이 존재하지 않으면
	{
		sprintf(buf, "[server] : invalidroomId\n");	// 못들어 간다. 에러 메시지 작성
	}
	else	// 방을 찾았으면
	{
		client->roomId = roomId;	// 클라이언트는 방의 ID를 가지게 된다.
		sprintf(buf, "[server] : **%s Join the Room**\n", client->name); // 확인 메시지 작성
		sendMessageUser(her, client->socket);
	}

	// 결과 메시지를 클라이언트에게 소켓으로 돌려준다.
	sendMessageRoom(buf, client->roomId);
	//sendMessageUser(her, client->socket);
}

// 방 만들기 함수
void createRoom(Client * client)	// 특정 사용자가 방을 개설한다.
{
	int i;
	char name[BUF_SIZE];
	char originRoomname[BUF_SIZE];
	char cmpRoomname[BUF_SIZE];

	char buf[BUF_SIZE] = "";	// 사용자에게 돌려줄 메시지 임시 버퍼 초기화
	sprintf(buf, "[server] : Input The Room Name:\n");	

	sendMessageUser(buf, client->socket);	// 방이름을 바로 받아 저장한다.

											



	if (read(client->socket, buf, BUF_SIZE)>0)	// 잘 받았으면
	{
		for (i = 0; i < sizeRoom; i++)      // 모든 방에 대해서
		{
			sscanf(arrRoom[i].name, "%s %s", name, originRoomname);
			sscanf(buf, "%s %s", name, cmpRoomname);
			if (strcmp(originRoomname, cmpRoomname) == 0) {    // 이름이 같은 방을 찾았으면 들어간다.
				enterRoom(client, arrRoom[i].id);
				return;
			}
		}

		Room * room = addRoom(buf);			// 방을 하나 만들고
		enterRoom(client, room->id);		// 사용자는 방에 들어간다.
	}
}


// 방의 목록을 표시하라.
void listRoom(Client * client)	// 특정 사용자가 방의 목록을 보고 싶어한다.
{
	char buf[BUF_SIZE] = "";	// 클라이언트에게 전송할 메시지 버퍼
	int i = 0;					// 제어 변수

	sprintf(buf, "[server] : List Room:\n");	// "방 목록을 표시하겠습니다."	
	sendMessageUser(buf, client->socket);	// 모든 방의 목록을 전송한다.

											
	for (i = 0; i<sizeRoom; i++)	// 모든 방에 대해서	
	{
		Room * room = &(arrRoom[i]);	// 각각의 방을 들고와서
										// ID와 이름의 형태로 개행문자를 넣어 한줄씩 전송한다.
		sprintf(buf, "RoomName : %s \n", room->name);
		sendMessageUser(buf, client->socket);
	}

	// 끝나면 총 방의 갯수를 표시한다.
	sprintf(buf, "Total %d rooms\n", sizeRoom);
	sendMessageUser(buf, client->socket);
}

// 특정 방에 있는 사용자의 목록을 표시한다.
void listMember(Client * client, int roomId) // list client in a room
{
	char buf[BUF_SIZE] = "";		// 사용자에게 전송할 메시지용 버퍼
	int i = 0;					// 제어변수
	int sizeMember = 0;			// 접속중인 사용자의 수

								
	sprintf(buf, "[server] : List Member In This Room\n");
	sendMessageUser(buf, client->socket); // 안내 메시지 표시

	for (i = 0; i<sizeClient; i++)	// 모든 사용자에 대해서
	{
		if (arrClient[i].roomId == roomId)	// 만약 특정 방에 들어가 있으면
		{
			// 번호, 소켓번호(고객의 ID) 순으로 표시해 준다.
			sprintf(buf, "Name : %s\n", arrClient[i].name);
			sendMessageUser(buf, client->socket);
			sizeMember++;	// 특정 방에 있는 총 사용자의 수 계산
		}
	}

	// 특정 방에 있는 사용자의 수를 표시한다.
	sprintf(buf, "Total: %d Members\n", sizeMember);
	sendMessageUser(buf, client->socket);
}

int getRoomId(int socket)      // socket은 클라이언트 ID
{
	int i, roomId = -1;         // 방의 ID 초기값은 -1(못찾음)
	char buf[BUF_SIZE] = "";   // 사용자에게 보낼 메시지 버퍼
	char Roomname[BUF_SIZE] = "";
	char originRoomname[BUF_SIZE] = "";
	// 안내 메시지 표시
	sprintf(buf, "[system] : Input Room Name:\n");   // "방의 ID를 입력하시오"
	sendMessageUser(buf, socket);         // 사용자에게 메시지 전송

	if (read(socket, buf, sizeof(buf))>0)      // 방의 ID를 입력 받는다.
	{
		char name[BUF_SIZE] = "";
		sscanf(buf, "%s %s", name, Roomname);   
	}

	for (i = 0; i < sizeRoom; i++)      // 모든 방에 대해서
	{
		sscanf(arrRoom[i].name, "%s %s", buf, originRoomname);
		if (strcmp(originRoomname, Roomname) == 0)   // 만약에 특정 방을 찾았으면 방의 번호 반환
			return arrRoom[i].id;    
	}

	return roomId;   // 없으면 에러의 방의 번호 반환
}


// 클라이언트에게 대기실 메뉴를 보여 준다.
void printWaitingRoomMenu(Client * client)
{
	char buf[BUF_SIZE] = "";
	sprintf(buf, "[system] : Waiting Room Menu:\n");	// 대기실 메뉴는 이렇습니다.
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "1) Create Room\n");				// 방 만들기
	sendMessageUser(buf, client->socket);

	sprintf(buf, "2) List Room\n");					// 방 목록 표시하기
	sendMessageUser(buf, client->socket);

	sprintf(buf, "3) Enter Room\n");				// 특정 방에 들어가기
	sendMessageUser(buf, client->socket);

	sprintf(buf, "4) Info Room\n");					// 방 정보 표시
	sendMessageUser(buf, client->socket);

	sprintf(buf, "5) How To Use\n");
	sendMessageUser(buf, client->socket);

	sprintf(buf, "q Or Q) Quit\n");						// 종료하기(연결 끊기)
	sendMessageUser(buf, client->socket);
}

// 채팅 방에서 사용가능한 메뉴 표시하기
void printRoomMenu(Client * client)
{
	char buf[BUF_SIZE] = "";
	sprintf(buf, "[system] : Room Menu:\n");		// 방 메뉴 표시
	sendMessageUser(buf, client->socket);

	sprintf(buf, "exit) Exit Room\n");			// 방 나가기
	sendMessageUser(buf, client->socket);

	sprintf(buf, "list) List Room\n");			// 개설된 방의 목록 표시하기
	sendMessageUser(buf, client->socket);

	sprintf(buf, "help) This message\n");		// 도움말 표시하기
	sendMessageUser(buf, client->socket);
}

void printHowToUse( Client * client)
{
	char buf[BUF_SIZE]="";
	sprintf(buf ,"**Menu**\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "1.CreateRoom : Create chat room and enter the room\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "2.ListRoom : Show all created chat rooms\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "3.EnterRoom : Enter the chat room\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "4.InfoRoom : Show all users in certain chat room\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "**Commands in ChatRoom**\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "1. exit : Exit room\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "2. list : Users in the chat room\n");
	sendMessageUser(buf, client->socket);
	
	sprintf(buf, "3. help : Manual of Commands in chat room\n");
	sendMessageUser(buf, client->socket);

}

// 대기실 메뉴에서 사용자가 선택을 하면 서비스를 제공한다.
void serveWaitingRoomMenu(int menu, Client * client, char * msg) // 사용자와 선택된 메뉴
{
	int roomId = ROOM_ID_DEFAULT;
	switch (menu)	// 여러개의 메뉴 중에서 하나를 선택하는 경우 select 문이 유용하다.
	{
	case 1:
		createRoom(client);					// 1번 방 만들기
		break;
	case 2:
		listRoom(client);					// 2번 방 목록 표시하기
		break;
	case 3:
		roomId = getRoomId(client->socket);	// 3번 특정 방으로 이동하기
		enterRoom(client, roomId);
		break;
	case 4:
		roomId = getRoomId(client->socket);	// 4번 특정 방에 있는 사용자 목록 보기
		listMember(client, roomId);
		break;
	case 5:
		//printWaitingRoomMenu(client);
		printHowToUse(client);
		break;

	default:
		sendMessageUser(msg, client->socket);
		//printWaitingRoomMenu(client);		// 잘못 입력했으면, 메뉴 다시 표시
		break;
	}
}

// 현재 방을 빠져 나가기
void exitRoom(Client * client)	// 인자는 나갈 클라이언트
{
	int roomId = client->roomId;			// 현재 방 번호
	client->roomId = ROOM_ID_DEFAULT;		// 사용자의 현재 방을 초기화 한다.

	char buf[BUF_SIZE]="";					// 메시지 버퍼
	sprintf(buf,"[server] exited room id:%d\n", roomId); //"특정 방을 나왔습니다."
	sendMessageUser(buf, client->socket);	// 메시지 전송

	printWaitingRoomMenu(client);

	if (isEmptyRoom(roomId))			// 방에 아무도 없으면
	{
		removeRoom(roomId);		// 방을 소멸 시킨다.
	}
}

// 방에서 메뉴를 선택했을 때 제공할 서비스(서버가 해야 할 일)
void serveRoomMenu(char * menu, Client * client, char * msg)
{
	char buf[BUF_SIZE] = "";
	printf("Server Menu : %s\n", menu);	// 서버 쪽에 찍히는 메시지(디버깅용)

	if (strcmp(menu, "exit") == 0) {			// exit - 방 나가기
		sprintf(buf, "%s out\n", client->name);
		sendMessageRoom(buf, client->roomId);
		exitRoom(client);
	}
	else if (strcmp(menu, "list") == 0) {			// list - 같은 방에 있는 사람 목록 표시
		listMember(client, client->roomId);
	}
	else if (strcmp(menu, "help") == 0) {				// help - 방의 메뉴 표시
		printRoomMenu(client);
	}
	else    // send normal chat message
		sendMessageRoom(msg, client->roomId);	// 일반 채팅 메시지
}

// 특정 방에 사람이 있습니까?
BOOL isEmptyRoom(int roomId)
{
	int i = 0;
	for (i = 0; i<sizeClient; i++)		// 모든 클라이언트 중에
	{
		if (arrClient[i].roomId == roomId)	// 한명이라도 이 방에 있으면
			return FALSE;			// 빈방이 아닙니다.
	}
	return TRUE;					// 빈방입니다.
}

// 쓰레드용 함수
void * handle_clnt(void * arg)	// 소켓을 들고 클라이언트와 통신하는 함수
{
	Client * client = (Client *)arg;	// 쓰레드가 통신할 클라이언트 구조체 변수
	int str_len = 0, i;
	char msg[BUF_SIZE];			// 메시지 버퍼

								// is in the waiting room on start chat
	printWaitingRoomMenu(client);	// 채팅을 시작할 때 처음에 방 메뉴 표시

	int clnt_sock = client->socket;	// 통신할 소켓 얻고
	int roomId = client->roomId;	// 방의 번호도 빼 놓는다.

									// 클라이언트가 통신을 끊지 않는한 계속 서비스를 제공한다.
	while ((str_len = read(clnt_sock, msg, BUF_SIZE)) != 0)
	{
		printf("Read User(%d):%s\n", clnt_sock, msg); // 디버깅용으로 상태로그 표시
														  // if is in a room
		if (isInARoom(clnt_sock))	// 방 안에서 클라이언트가 메시지를 보내는 경우
		{
			char menu[BUF_SIZE] = "";
			getSelectedRoomMenu(menu, msg);		// 방 메뉴 중에 어떤 선택을 했는지 판단
			serveRoomMenu(menu, client, msg);	// 방의 서비스 제공(exit, list, chat)
		}
		// is in the waiting room
		// 대기실에서 클라이언트가 메시지를 보내는 경우
		else
		{
			// 대기실의 메뉴를 입력 받는다(int인 이유는 1~4 선택 메뉴)
			int selectedMenu = getSelectedWaintingRoomMenu(msg);

			// 디버깅 목적으로 서버측 화면에 표시해 준다.
			printf("User(%d) selected menu(%d)\n", client->socket, selectedMenu);

			// 대기실 메뉴의 서비스를 제공한다.
			serveWaitingRoomMenu(selectedMenu, client, msg);
		}
	}

	removeClient(clnt_sock);	// 클라언트와 연결이 종료되면 클라이언트 배열에서 제거한다.

	close(clnt_sock);			// 소켓을 닫고
	return NULL;				// 쓰레드 종료
}

// 예외 처리 함수
void error_handling(char * msg)
{
	fputs(msg, stderr);		// 에러를 출력하고
	fputc('\n', stderr);
	exit(1);				// 프로그램 종료
}

