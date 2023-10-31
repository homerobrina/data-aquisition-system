#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32]; // supondo um ID de sensor de até 32 caracteres
    std::time_t timestamp; // timestamp UNIX
    double value; // valor da leitura
};
#pragma pack(pop)

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    read_message();
  }

private:
  void read_message()
  {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::istream is(&buffer_);
            std::string message(std::istreambuf_iterator<char>(is), {});
            std::vector<std::string> parts;
            message.resize(message.size() - 2);
            boost::split(parts, message, boost::is_any_of("|"));
            if(parts[0] == "LOG"){
              LogRecord measure;
                if (parts[1].size() < sizeof(measure.sensor_id)) {
                    std::strcpy(measure.sensor_id, parts[1].c_str());
                } else {
                    std::cout << "Sensor ID too long." << std::endl;
                }
              measure.timestamp = string_to_time_t(parts[2]);
              measure.value = std::stod(parts[3]);
              std::fstream file(parts[1]+".dat", std::fstream::out | std::fstream::binary 
																	 | std::fstream::app);
              if(file.is_open()){
                // Escreve o registro
                file.write((char*)&measure, sizeof(LogRecord));
              }
              // Envia echo para sensor
              write_message(message);
            } else if(parts[0] == "GET"){
              std::fstream file(parts[1]+".dat", std::fstream::in | std::fstream::binary 
																	 | std::fstream::app);
              if(file.is_open()){
                // Confere tamanho do arquivo
                file.seekg(0, std::fstream::end);
                int file_size = file.tellg();
                int n = file_size/sizeof(LogRecord);
                if(stoi(parts[2]) < n){
                    // Posiciona o cursor onde ele deve começar a ler
                    file.seekg((-stoi(parts[2]))*sizeof(LogRecord), std::ios_base::end);
                    std::string response = parts[2];
                    for (int i = 0; i < stoi(parts[2]); i++){
                      LogRecord rec;
                      file.read((char*)&rec, sizeof(LogRecord));
                      // Redige a reposta que será enviada para o cliente
                      response = response + ";" + time_t_to_string(rec.timestamp) + "|" + std::to_string(rec.value);
                    }
                    // Envia resposta
                    write_message(response + "\r\n");
                } else {
                    file.seekg(0, std::ios_base::beg);
                    std::cout << "Num records: " << n << " < Num req: " << parts[2] << std::endl;
                    std::string response = std::to_string(n);
                    for (int i = 0; i < n; i++){
                      LogRecord rec;
                      file.read((char*)&rec, sizeof(LogRecord));
                      response = response + ";" + time_t_to_string(rec.timestamp) + "|" + std::to_string(rec.value);
                    }
                    
                    // Envia resposta
                    write_message(response + "\r\n");
                }
              }
          }
          }
        });
  }

  void write_message(const std::string& message)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(message),
        [this, self, message](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_message();
          }
        });
  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    accept();
  }

private:
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}
