#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <memory>
#include <random>
#include <cstring>
#include <cstdlib>
#include <cstdint>

// Сетевые заголовки Linux
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

// Библиотека OpenSSL (MD5)
#include <openssl/md5.h>

class CryptoModule {
public:
    static uint64_t generateSalt64() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;
        return dis(gen);
    }

    static std::string toHex16(uint64_t salt) {
        std::stringstream ss;
        ss << std::setw(16) 
           << std::setfill('0') 
           << std::uppercase 
           << std::hex 
           << salt;
        return ss.str();
    }

    static std::string calculateMD5(
        const std::string& salt16, 
        const std::string& password) 
    {
        std::string input = salt16 + password;
        unsigned char digest[MD5_DIGEST_LENGTH];
        
        MD5(reinterpret_cast<const unsigned char*>(input.c_str()), 
            input.length(), 
            digest);
        
        std::stringstream ss;
        for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
            ss << std::setw(2) 
               << std::setfill('0') 
               << std::uppercase 
               << std::hex 
               << (int)digest[i];
        }
        return ss.str();
    }
};
class ConfigParser {
private:
    std::string serverAddress;
    int serverPort = 33333;
    std::string inputFile;
    std::string outputFile;
    std::string configFile = "";
    std::string login = "user";
    std::string password = "password";

    std::string getDefaultConfigPath() {
        const char* home = getenv("HOME");
        if (!home) return ".vclient.conf";
        return std::string(home) + "/.config/vclient.conf";
    }

public:
    ConfigParser() {
        configFile = getDefaultConfigPath();
    }

    void printHelp() {
        std::cout << "Использование: vclient -a <addr> -i <in> -o <out>\n"
                  << "Опции:\n"
                  << "  -a ADDRESS     Адрес сервера\n"
                  << "  -p PORT        Порт (дефолт: 33333)\n"
                  << "  -i INPUT_FILE  Входной текстовый файл\n"
                  << "  -o OUTPUT_FILE Бинарный файл вывода\n"
                  << "  -c CONFIG_FILE Файл аккаунта\n"
                  << "  -h             Справка\n";
    }

    bool parseCommandLine(int argc, char* argv[]) {
        if (argc == 1) {
            printHelp();
            exit(0);
        }
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h") {
                printHelp();
                exit(0);
            } else if (arg == "-a" && i + 1 < argc) {
                serverAddress = argv[++i];
            } else if (arg == "-p" && i + 1 < argc) {
                serverPort = std::stoi(argv[++i]);
            } else if (arg == "-i" && i + 1 < argc) {
                inputFile = argv[++i];
            } else if (arg == "-o" && i + 1 < argc) {
                outputFile = argv[++i];
            } else if (arg == "-c" && i + 1 < argc) {
                configFile = argv[++i];
            } else {
                return false; 
            }
        }
        return !serverAddress.empty() && 
               !inputFile.empty() && 
               !outputFile.empty();
    }

    bool readConfigFile() {
        std::ifstream f(configFile);
        if (!f.is_open()) return true; 
        if (!(f >> login >> password)) return false;
        return true;
    }

    std::string getServerAddress() const { return serverAddress; }
    int getServerPort() const { return serverPort; }
    std::string getInputFile() const { return inputFile; }
    std::string getOutputFile() const { return outputFile; }
    std::string getLogin() const { return login; }
    std::string getPassword() const { return password; }
};
class FileManager {
public:
    static bool parseTextFile(
        const std::string& path, 
        std::vector<std::vector<int16_t>>& vectors) 
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        uint32_t vectorCount = 0;
        if (!(file >> vectorCount)) return false;

        vectors.clear();
        for (uint32_t i = 0; i < vectorCount; ++i) {
            uint32_t vectorSize = 0;
            if (!(file >> vectorSize)) break;

            std::vector<int16_t> currentVector;
            for (uint32_t j = 0; j < vectorSize; ++j) {
                int16_t val;
                if (file >> val) {
                    currentVector.push_back(val);
                } else {
                    return false; 
                }
            }
            vectors.push_back(currentVector);
        }
        return true;
    }

    static bool writeBinaryResults(
        const std::string& path, 
        const std::vector<int16_t>& results) 
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t count = static_cast<uint32_t>(results.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        if (count > 0) {
            file.write(
                reinterpret_cast<const char*>(results.data()), 
                count * sizeof(int16_t)
            );
        }
        return file.good();
    }
};
class NetworkClient {
private:
    int socketFd = -1;
    bool isConnected = false;

    uint32_t hton32(uint32_t val) { return htonl(val); }
    uint16_t hton16(uint16_t val) { return htons(val); }
    uint16_t ntoh16(uint16_t val) { return ntohs(val); }

public:
    ~NetworkClient() { disconnect(); }

    bool connectToServer(const std::string& address, int port) {
        socketFd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd < 0) return false;

        struct sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        if (inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(address.c_str());
            if (!he) return false;
            std::memcpy(&serverAddr.sin_addr, he->h_addr_list, he->h_length);
        }

        if (connect(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            close(socketFd);
            socketFd = -1;
            return false;
        }
        isConnected = true;
        return true;
    }

    bool authenticate(
        const std::string& login, 
        const std::string& salt16, 
        const std::string& hashMD5) 
    {
        if (!isConnected) return false;
        std::string msg = login + " " + salt16 + " " + hashMD5 + "\n";
        
        if (send(socketFd, msg.c_str(), msg.length(), 0) < 0) return false;

        char buffer[128];
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRecv = recv(socketFd, buffer, sizeof(buffer) - 1, 0);
        if (bytesRecv <= 0) return false;

        std::string response(buffer);
        return (response.find("OK") != std::string::npos);
    }

    bool transferVectorsAndGetResults(
        const std::vector<std::vector<int16_t>>& vectors, 
        std::vector<int16_t>& results) 
    {
        if (!isConnected) return false;

        uint32_t countRaw = static_cast<uint32_t>(vectors.size());
        uint32_t vectorCount = hton32(countRaw);
        if (send(socketFd, &vectorCount, sizeof(vectorCount), 0) != 
            sizeof(vectorCount)) return false;

        results.clear();
        for (const auto& vec : vectors) {
            uint32_t sizeRaw = static_cast<uint32_t>(vec.size());
            uint32_t vecSize = hton32(sizeRaw);
            if (send(socketFd, &vecSize, sizeof(vecSize), 0) != 
                sizeof(vecSize)) return false;

            std::vector<int16_t> netVectors;
            for (int16_t val : vec) {
                uint16_t uval = static_cast<uint16_t>(val);
                netVectors.push_back(hton16(uval));
            }

            size_t byteSize = netVectors.size() * sizeof(int16_t);
            if (send(socketFd, netVectors.data(), byteSize, 0) != 
                (ssize_t)byteSize) return false;

            int16_t rawResult;
            if (recv(socketFd, &rawResult, sizeof(rawResult), MSG_WAITALL) != 
                sizeof(rawResult)) return false;
            
            uint16_t ures = static_cast<uint16_t>(rawResult);
            results.push_back(static_cast<int16_t>(ntoh16(ures)));
        }
        return true;
    }

    void disconnect() {
        if (isConnected && socketFd != -1) {
            close(socketFd);
            socketFd = -1;
            isConnected = false;
        }
    }
};
class MainApplication {
private:
    void terminateWithError(
        const std::string& functionName, 
        const std::string& errorType, 
        const std::string& details) 
    {
        std::cerr << "КРИТИЧЕСКАЯ ОШИБКА!\n"
                  << "Функция: " << functionName << "\n"
                  << "Тип ошибки: " << errorType << "\n"
                  << "Детали: " << details << "\n";
        exit(1);
    }

public:
    int run(int argc, char* argv[]) {
        ConfigParser parser;
        if (!parser.parseCommandLine(argc, argv)) {
            terminateWithError("parseCommandLine", "InvalidArgs", "Нет -a, -i, -o");
        }
        if (!parser.readConfigFile()) {
            terminateWithError("readConfigFile", "CfgError", "Ошибка файла конфига");
        }

        std::vector<std::vector<int16_t>> inputVectors;
        if (!FileManager::parseTextFile(parser.getInputFile(), inputVectors)) {
            terminateWithError("parseTextFile", "InFileError", "Ошибка файла векторов");
        }

        NetworkClient client;
        if (!client.connectToServer(parser.getServerAddress(), parser.getServerPort())) {
            terminateWithError("connectToServer", "Refused", "Нет связи с сервером");
        }

        uint64_t rawSalt = CryptoModule::generateSalt64();
        std::string salt16 = CryptoModule::toHex16(rawSalt);
        std::string hashMD5 = CryptoModule::calculateMD5(salt16, parser.getPassword());

        if (!client.authenticate(parser.getLogin(), salt16, hashMD5)) {
            terminateWithError("authenticate", "AuthFailed", "Отказ авторизации");
        }

        std::vector<int16_t> results;
        if (!client.transferVectorsAndGetResults(inputVectors, results)) {
            terminateWithError("transferVectors", "NetError", "Сбой обмена данными");
        }

        client.disconnect();

        if (!FileManager::writeBinaryResults(parser.getOutputFile(), results)) {
            terminateWithError("writeBinaryResults", "OutError", "Ошибка записи вывода");
        }

        std::cout << "Выполнение успешно завершено." << std::endl;
        return 0;
    }
};

int main(int argc, char* argv[]) {
    MainApplication app;
    return app.run(argc, argv);
}
