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
            boost::split(parts, message, boost::is_any_of("|"));

            if(parts[0].compare("LOG") == 0){
              LogRecord measure;
                if (parts[1].size() < sizeof(measure.sensor_id)) {
                    std::strcpy(measure.sensor_id, parts[1].c_str());
                } else {
                    std::cerr << "Sensor ID too long." << std::endl;
                }
              measure.timestamp = string_to_time_t(parts[2]);
              measure.value = std::stod(parts[3]);
              std::fstream file(parts[1]+".dat", std::fstream::out | std::fstream::in | std::fstream::binary 
																	 | std::fstream::app);
              if(file.is_open()){
                file.write((char*)&measure, sizeof(LogRecord));
              }

              std::cout << "ID escrito: " << measure.sensor_id << std::endl;
              std::cout << "Data escrito: " << measure.timestamp << std::endl;
              std::cout << "Valor escrito: " << measure.value << std::endl;
              std::cout << std::endl;              
            } else if(parts[0].compare("GET") == 0){
              std::fstream file(parts[1]+".dat", std::fstream::out | std::fstream::in | std::fstream::binary 
																	 | std::fstream::app);
              if(file.is_open()){
                file.seekp((1)*sizeof(LogRecord), std::ios_base::beg);
                // Le o registro selecionado
                LogRecord rec;
                file.read((char*)&rec, sizeof(LogRecord));
                std::cout << "ID lido: " << rec.sensor_id << std::endl;
                std::cout << "Data lido: " << rec.timestamp << std::endl;
                std::cout << "Valor lido: " << rec.value << std::endl;
                std::cout << "Data em str: " << time_t_to_string(rec.timestamp) << std::endl;
              }
              
              // LogRecord measure;
              //   if (parts[1].size() < sizeof(measure.sensor_id)) {
              //       std::strcpy(measure.sensor_id, parts[1].c_str());
              //   } else {
              //       std::cerr << "Sensor ID too long." << std::endl;
              //   }
              // measure.timestamp = string_to_time_t(parts[2]);
              // measure.value = std::stod(parts[3]);
          }
            write_message(message);
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
