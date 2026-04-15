# DB 접속 정보

## 접속 정보

| 항목 | 값 |
|------|-----|
| Host | `10.10.10.130` |
| User | `factorymanager` |
| Password | `1234` |
| Database | `Factory` |

## 접속 명령어

```
mysql -h 10.10.10.130 -u factorymanager -p Factory
```

비밀번호 입력 프롬프트가 뜨면 `1234` 입력.

## 코드에 반영

`MainServer/src/Main.cpp`의 `DbManager` 생성자에 동일 값 반영 필요:

```cpp
DbManager db_manager(event_bus, "10.10.10.130", "factorymanager", "1234", "Factory");
```

(현재는 `127.0.0.1` / `factory_qc`로 placeholder 상태)
