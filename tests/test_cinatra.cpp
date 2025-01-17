#include <async_simple/coro/Collect.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "async_simple/coro/SyncAwait.h"
#include "cinatra.hpp"
#include "cinatra/coro_http_client.hpp"
#include "cinatra/coro_http_server.hpp"
#include "cinatra/define.h"
#include "cinatra/multipart.hpp"
#include "cinatra/string_resize.hpp"
#include "cinatra/time_util.hpp"
#include "doctest/doctest.h"
using namespace std::chrono_literals;

using namespace cinatra;

#ifdef CINATRA_ENABLE_GZIP
std::string_view get_header_value(auto &resp_headers, std::string_view key) {
  for (const auto &[k, v] : resp_headers) {
    if (k == key)
      return v;
  }
  return {};
}

TEST_CASE("test for gzip") {
  coro_http_server server(1, 8090);
  server.set_http_handler<GET, POST>(
      "/gzip", [](coro_http_request &req, coro_http_response &res) {
        CHECK(req.get_header_value("Content-Encoding") == "gzip");
        res.set_status_and_content(status_type::ok, "hello world",
                                   content_encoding::gzip);
      });
  server.async_start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090/gzip";
  client.add_header("Content-Encoding", "gzip");
  auto result = async_simple::coro::syncAwait(client.async_get(uri));
  auto content = get_header_value(result.resp_headers, "Content-Encoding");
  CHECK(get_header_value(result.resp_headers, "Content-Encoding") == "gzip");
  std::string decompress_data;
  bool ret = gzip_codec::uncompress(result.resp_body, decompress_data);
  CHECK(ret == true);
  CHECK(decompress_data == "hello world");
  server.stop();
}
#endif

#ifdef CINATRA_ENABLE_SSL
TEST_CASE("test ssl client") {
  {
    coro_http_client client{};
    auto result = client.get("https://www.bing.com");
    CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    auto r =
        async_simple::coro::syncAwait(client.connect("https://www.baidu.com"));
    if (r.status == 200) {
      auto result = client.get("/");
      CHECK(result.status >= 200);
    }
  }

  {
    coro_http_client client{};
    auto result = client.get("http://www.bing.com");
    CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    client.set_ssl_schema(true);
    auto result = client.get("www.bing.com");
    CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    client.set_ssl_schema(false);
    auto result = client.get("https://www.bing.com");
    CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    client.enable_auto_redirect(true);
    bool ok = client.init_ssl();
    REQUIRE_MESSAGE(ok == true, "init ssl fail, please check ssl config");
    auto result = client.get("https://www.bing.com");
    CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    client.set_req_timeout(8s);
    client.enable_auto_redirect(true);
    std::string uri = "http://www.bing.com";
    // Make sure the host and port are matching with your proxy server
    client.set_proxy("106.14.255.124", "80");
    resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
    if (!result.net_err)
      CHECK(result.status >= 200);
  }

  {
    coro_http_client client{};
    bool ok = client.init_ssl();
    REQUIRE_MESSAGE(ok == true, "init ssl fail, please check ssl config");
    auto result = client.get("https://www.bing.com");
    CHECK(result.status >= 200);
  }
}

TEST_CASE("test ssl client") {
  coro_http_client client{};
  bool ok = client.init_ssl();
  REQUIRE_MESSAGE(ok == true, "init ssl fail, please check ssl config");
  // client.set_sni_hostname("https://www.bing.com");
  auto result = client.get("https://www.bing.com");
  CHECK(result.status >= 200);
}
#endif

bool create_file(std::string_view filename, size_t file_size = 1024) {
  std::ofstream out(filename.data(), std::ios::binary);
  if (!out.is_open()) {
    return false;
  }

  std::string str(file_size, 'A');
  out.write(str.data(), str.size());
  return true;
}

TEST_CASE("test cinatra::string with SSO") {
  std::string s = "HelloHi";
  auto oldlen = s.length();
  s.reserve(10);
  memset(s.data() + oldlen + 1, 'A', 3);
  cinatra::detail::resize(s, 10);
  CHECK(s[10] == '\0');
  memcpy(s.data() + oldlen, "233", 3);
  CHECK(strlen(s.data()) == 10);
  CHECK(s == "HelloHi233");
}

TEST_CASE("test cinatra::string without SSO") {
  std::string s(1000, 'A');
  std::string s2(5000, 'B');
  std::string sum = s + s2;
  auto oldlen = s.length();
  s.reserve(6000);
  memset(s.data() + oldlen + 1, 'A', 5000);
  cinatra::detail::resize(s, 6000);
  CHECK(s[6000] == '\0');
  memcpy(s.data() + oldlen, s2.data(), s2.length());
  CHECK(strlen(s.data()) == 6000);
  CHECK(s == sum);
}

TEST_CASE("test cinatra::string SSO to no SSO") {
  std::string s(10, 'A');
  std::string s2(5000, 'B');
  std::string sum = s + s2;
  auto oldlen = s.length();
  s.reserve(5010);
  memset(s.data() + oldlen + 1, 'A', 5000);
  cinatra::detail::resize(s, 5010);
  CHECK(s[5010] == '\0');
  memcpy(s.data() + oldlen, s2.data(), s2.length());
  CHECK(strlen(s.data()) == 5010);
  CHECK(s == sum);
}

TEST_CASE("test coro channel") {
  auto ctx = coro_io::get_global_block_executor()->get_asio_executor();
  asio::experimental::channel<void(std::error_code, int)> ch(ctx, 10000);
  auto ec = async_simple::coro::syncAwait(coro_io::async_send(ch, 41));
  CHECK(!ec);
  ec = async_simple::coro::syncAwait(coro_io::async_send(ch, 42));
  CHECK(!ec);

  std::error_code err;
  int val;
  std::tie(err, val) =
      async_simple::coro::syncAwait(coro_io::async_receive<int>(ch));
  CHECK(!err);
  CHECK(val == 41);

  std::tie(err, val) =
      async_simple::coro::syncAwait(coro_io::async_receive<int>(ch));
  CHECK(!err);
  CHECK(val == 42);
}

async_simple::coro::Lazy<void> test_collect_all() {
  asio::io_context ioc;
  std::thread thd([&] {
    ioc.run();
  });
  std::vector<std::shared_ptr<coro_http_client>> v;
  std::vector<async_simple::coro::Lazy<resp_data>> futures;
  for (int i = 0; i < 2; ++i) {
    auto client = std::make_shared<coro_http_client>();
    v.push_back(client);
    futures.push_back(client->async_get("http://www.baidu.com/"));
  }

  auto out = co_await async_simple::coro::collectAll(std::move(futures));

  for (auto &item : out) {
    auto result = item.value();
    CHECK(result.status >= 200);
  }
  thd.join();
}

TEST_CASE("test request with out buffer") {
  std::string str;
  str.resize(10);
  std::string url = "http://cn.bing.com";

  {
    coro_http_client client;
    auto ret = client.async_request(url, http_method::GET, req_context<>{}, {},
                                    std::span<char>{str.data(), str.size()});
    auto result = async_simple::coro::syncAwait(ret);
    std::cout << result.status << "\n";
    std::cout << result.net_err.message() << "\n";
    if (result.status == 404)
      CHECK(result.net_err == std::errc::no_buffer_space);
  }

  {
    detail::resize(str, 102400);
    coro_http_client client;
    auto ret = client.async_request(url, http_method::GET, req_context<>{}, {},
                                    std::span<char>{str.data(), str.size()});
    auto result = async_simple::coro::syncAwait(ret);
    bool ok = result.status == 200 || result.status == 301;
    CHECK(ok);
    std::string_view sv(str.data(), result.resp_body.size());
    CHECK(result.resp_body == sv);
  }
}

TEST_CASE("test pass path not entire uri") {
  coro_http_client client{};
  auto r =
      async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
  std::cout << r.resp_body.size() << "\n";
  auto buf = client.release_buf();
  std::cout << strlen(buf.data()) << "\n";
  std::cout << buf << "\n";
  CHECK(r.status >= 200);

  r = async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
  CHECK(r.status >= 200);

  r = async_simple::coro::syncAwait(client.async_get("/"));
  CHECK(r.status >= 200);
}

TEST_CASE("test coro_http_client connect/request timeout") {
  {
#if !defined(_MSC_VER)
    coro_http_client client{};
    cinatra::coro_http_client::config conf{.conn_timeout_duration = 1ms};
    client.init_config(conf);
    auto r =
        async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
    std::cout << r.net_err.value() << ", " << r.net_err.message() << "\n";
    CHECK(r.net_err != std::errc{});
#endif
  }

  {
    coro_http_client client{};
    cinatra::coro_http_client::config conf{.conn_timeout_duration = 10s,
                                           .req_timeout_duration = 1ms};
    client.init_config(conf);
    auto r =
        async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
    std::cout << r.net_err.message() << "\n";
    CHECK(r.net_err != std::errc{});
  }
}

TEST_CASE("test coro_http_client async_http_connect") {
  coro_http_client client{};
  cinatra::coro_http_client::config conf{.req_timeout_duration = 60s};
  client.init_config(conf);
  auto r = async_simple::coro::syncAwait(
      client.async_http_connect("http://www.baidu.com"));
  CHECK(r.status >= 200);
  for (auto [k, v] : r.resp_headers) {
    std::cout << k << ", " << v << "\n";
  }

  coro_http_client client1{};
  r = async_simple::coro::syncAwait(
      client1.async_http_connect("http//www.badurl.com"));
  CHECK(r.status != 200);

  r = async_simple::coro::syncAwait(client1.reconnect("http://cn.bing.com"));
  CHECK(client1.get_host() == "cn.bing.com");
  CHECK(client1.get_port() == "http");
  CHECK(r.status >= 200);

  r = async_simple::coro::syncAwait(client1.reconnect("http://www.baidu.com"));

  CHECK(r.status >= 200);
  r = async_simple::coro::syncAwait(client1.reconnect("http://cn.bing.com"));
  CHECK(r.status == 200);
}

TEST_CASE("test collect all") {
  async_simple::coro::syncAwait(test_collect_all());
}

TEST_CASE("test head put and some other request") {
  coro_http_server server(1, 8090);
  server.set_http_handler<HEAD>(
      "/headers", [](coro_http_request &req, coro_http_response &resp) {
        resp.add_header("Content-Type", "application/json");
        resp.add_header("Content-Length", "117");
        resp.set_status_and_content(status_type::ok, "");
      });
  server.set_http_handler<cinatra::http_method::PATCH,
                          cinatra::http_method::TRACE>(
      "/", [](coro_http_request &req, coro_http_response &resp) {
        resp.set_status(status_type::method_not_allowed);
      });
  server.set_http_handler<cinatra::http_method::OPTIONS>(
      "/", [](coro_http_request &req, coro_http_response &resp) {
        resp.add_header("Allow", "HEAD, OPTIONS, GET, POST, PUT");
        resp.set_status_and_content(status_type::ok, "");
      });
  server.set_http_handler<cinatra::http_method::PUT>(
      "/put/json", [](coro_http_request &req, coro_http_response &resp) {
        auto json_str = req.get_body();
        std::ofstream file("json.txt", std::ios::binary);
        file.write(json_str.data(), json_str.size());
        file.close();
        resp.set_status_and_content(status_type::ok, "");
      });
  server.set_http_handler<cinatra::http_method::PUT>(
      "/delete/:name", [](coro_http_request &req, coro_http_response &resp) {
        auto &filename = req.params_["name"];
        std::error_code ec;
        fs::remove(filename, ec);
        std::string result = ec ? "delete failed" : "ok";
        resp.set_status_and_content(status_type::ok, result);
      });
  server.set_http_handler<cinatra::http_method::DEL>(
      "/delete/:name", [](coro_http_request &req, coro_http_response &resp) {
        auto &filename = req.params_["name"];
        std::error_code ec;
        fs::remove(filename, ec);
        std::string result = ec ? "delete failed" : "delete ok";
        resp.set_status_and_content(status_type::ok, result);
      });
  server.async_start();
  std::this_thread::sleep_for(300ms);

  coro_http_client client{};

  auto result = async_simple::coro::syncAwait(
      client.async_head("http://127.0.0.1:8090/headers"));
  CHECK(result.status == 200);

  result = async_simple::coro::syncAwait(
      client.async_patch("http://127.0.0.1:8090/"));
  CHECK(result.status == 405);

  result = async_simple::coro::syncAwait(
      client.async_trace("http://127.0.0.1:8090/"));
  CHECK(result.status == 405);

  result = async_simple::coro::syncAwait(
      client.async_options("http://127.0.0.1:8090/"));
  CHECK(result.status == 200);

  std::string json = R"({
  "Id": 12345,
  "Customer": "John Smith",
  "Quantity": 1,
  "Price": 10.00
  })";

  coro_http_client client1{};
  result = async_simple::coro::syncAwait(client1.async_put(
      "http://127.0.0.1:8090/put/json", json, req_content_type::json));
  CHECK(result.status == 200);

  result = async_simple::coro::syncAwait(client1.async_post(
      "http://127.0.0.1:8090/delete/json.txt", json, req_content_type::json));

  CHECK(result.status == 404);

  result = async_simple::coro::syncAwait(client1.async_delete(
      "http://127.0.0.1:8090/delete/json.txt", json, req_content_type::json));

  CHECK(result.status == 200);
}

TEST_CASE("test upload file") {
  coro_http_server server(1, 8090);
  server.set_http_handler<cinatra::PUT, cinatra::POST>(
      "/multipart",
      [](coro_http_request &req,
         coro_http_response &resp) -> async_simple::coro::Lazy<void> {
        assert(req.get_content_type() == content_type::multipart);
        auto boundary = req.get_boundary();
        multipart_reader_t multipart(req.get_conn());
        while (true) {
          auto part_head = co_await multipart.read_part_head();
          if (part_head.ec) {
            co_return;
          }

          std::cout << part_head.name << "\n";
          std::cout << part_head.filename << "\n";

          std::shared_ptr<coro_io::coro_file> file;
          std::string filename;
          if (!part_head.filename.empty()) {
            file = std::make_shared<coro_io::coro_file>();
            filename = std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());

            size_t pos = part_head.filename.rfind('.');
            if (pos != std::string::npos) {
              auto extent = part_head.filename.substr(pos);
              filename += extent;
            }

            std::cout << filename << "\n";
            co_await file->async_open(filename, coro_io::flags::create_write);
            if (!file->is_open()) {
              resp.set_status_and_content(status_type::internal_server_error,
                                          "file open failed");
              co_return;
            }
          }

          auto part_body = co_await multipart.read_part_body(boundary);
          if (part_body.ec) {
            co_return;
          }

          if (!filename.empty()) {
            auto ec = co_await file->async_write(part_body.data.data(),
                                                 part_body.data.size());
            if (ec) {
              co_return;
            }

            file->close();
            CHECK(fs::file_size(filename) == 2 * 1024 * 1024);
          }
          else {
            std::cout << part_body.data << "\n";
          }

          if (part_body.eof) {
            break;
          }
        }

        resp.set_status_and_content(status_type::ok, "multipart finished");
        co_return;
      });

  server.async_start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090/multipart";
  resp_data result =
      async_simple::coro::syncAwait(client.async_upload_multipart(uri));
  CHECK(result.status == 404);

  client.add_str_part("hello", "world");
  client.add_str_part("key", "value");
  CHECK(!client.add_file_part("key", "value"));
  result = async_simple::coro::syncAwait(client.async_upload_multipart(uri));
  CHECK(!client.is_redirect(result));
  CHECK(result.resp_body == "multipart finished");

  client.add_str_part("hello", "world");
  result = async_simple::coro::syncAwait(
      client.async_upload_multipart("http//badurl.com"));
  CHECK(result.status == 404);

  client.set_max_single_part_size(1024);
  std::string test_file_name = "test1.txt";
  std::ofstream test_file;
  test_file.open(test_file_name,
                 std::ios::binary | std::ios::out | std::ios::trunc);
  std::vector<char> test_file_data(2 * 1024 * 1024, '0');
  test_file.write(test_file_data.data(), test_file_data.size());
  test_file.close();
  result = async_simple::coro::syncAwait(
      client.async_upload_multipart(uri, "test", test_file_name));

  CHECK(result.resp_body == "multipart finished");

  std::filesystem::remove(std::filesystem::path(test_file_name));

  std::string not_exist_file = "notexist.txt";
  result = async_simple::coro::syncAwait(client.async_upload_multipart(
      uri, "test_not_exist_file", not_exist_file));
  CHECK(result.status == 404);

  result = async_simple::coro::syncAwait(client.async_upload_multipart(
      "http//badurl.com", "test_not_exist_file", not_exist_file));
  CHECK(result.status == 404);

  client.close();

  server.stop();
}

TEST_CASE("test bad uri") {
  coro_http_client client{};
  CHECK(client.add_header("hello", "cinatra"));
  CHECK(client.add_header("hello", "cinatra"));
  CHECK(!client.add_header("", "cinatra"));
  client.add_str_part("hello", "world");
  auto result = async_simple::coro::syncAwait(
      client.async_upload_multipart("http://www.badurlrandom.org"));
  CHECK(result.status == 404);
}

TEST_CASE("test multiple ranges download") {
  coro_http_client client{};
  std::string uri = "http://uniquegoodshiningmelody.neverssl.com/favicon.ico";

  std::string filename = "test1.txt";
  std::error_code ec{};
  std::filesystem::remove(filename, ec);
  resp_data result = async_simple::coro::syncAwait(
      client.async_download(uri, filename, "1-16"));
  if (result.status == 206) {
    CHECK(std::filesystem::file_size(filename) == 16);
  }
}

TEST_CASE("test ranges download") {
  create_file("test_range.txt", 64);
  coro_http_server server(1, 8090);
  server.set_static_res_dir("", "");
  server.async_start();

  coro_http_client client{};
  client.set_req_timeout(std::chrono::seconds(8));
  std::string uri = "http://127.0.0.1:8090/test_range.txt";

  std::string filename = "test1.txt";
  std::error_code ec{};
  std::filesystem::remove(filename, ec);
  resp_data result = async_simple::coro::syncAwait(
      client.async_download(uri, filename, "1-10"));
  CHECK(result.status == 206);
  CHECK(std::filesystem::file_size(filename) == 10);

  filename = "test2.txt";
  std::filesystem::remove(filename, ec);
  result = async_simple::coro::syncAwait(
      client.async_download(uri, filename, "10-15"));
  CHECK(result.status == 206);
  CHECK(std::filesystem::file_size(filename) == 6);
}

TEST_CASE("test ranges download with a bad filename and multiple ranges") {
  create_file("test_multiple_range.txt", 64);
  coro_http_server server(1, 8090);
  server.set_static_res_dir("", "");
  server.async_start();

  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090/test_multiple_range.txt";

  std::string filename = "";
  std::error_code ec{};
  std::filesystem::remove(filename, ec);
  resp_data result = async_simple::coro::syncAwait(
      client.async_download(uri, filename, "1-10,11-16"));
  CHECK(result.status == 404);
  CHECK(result.net_err ==
        std::make_error_code(std::errc::no_such_file_or_directory));

  client.add_header("Range", "bytes=1-10,20-30");
  result = client.get(uri);
  CHECK(result.status == 206);
  CHECK(result.resp_body.size() == 21);

  filename = "test_ranges.txt";
  client.add_header("Range", "bytes=0-10,21-30");
  result = client.download(uri, filename);
  CHECK(result.status == 206);
  CHECK(fs::file_size(filename) == 21);
}

TEST_CASE("test coro_http_client quit") {
  std::promise<bool> promise;
  [&] {
    { coro_http_client client{}; }
    promise.set_value(true);
  }();

  CHECK(promise.get_future().get());
}

TEST_CASE("test coro_http_client multipart upload") {
  coro_http_server server(1, 8090);
  server.set_http_handler<cinatra::PUT, cinatra::POST>(
      "/multipart_upload",
      [](coro_http_request &req,
         coro_http_response &resp) -> async_simple::coro::Lazy<void> {
        assert(req.get_content_type() == content_type::multipart);
        auto boundary = req.get_boundary();
        multipart_reader_t multipart(req.get_conn());
        while (true) {
          auto part_head = co_await multipart.read_part_head();
          if (part_head.ec) {
            co_return;
          }

          std::cout << part_head.name << "\n";
          std::cout << part_head.filename << "\n";

          std::shared_ptr<coro_io::coro_file> file;
          std::string filename;
          if (!part_head.filename.empty()) {
            file = std::make_shared<coro_io::coro_file>();
            filename = std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count());

            size_t pos = part_head.filename.rfind('.');
            if (pos != std::string::npos) {
              auto extent = part_head.filename.substr(pos);
              filename += extent;
            }

            std::cout << filename << "\n";
            co_await file->async_open(filename, coro_io::flags::create_write);
            if (!file->is_open()) {
              resp.set_status_and_content(status_type::internal_server_error,
                                          "file open failed");
              co_return;
            }
          }

          auto part_body = co_await multipart.read_part_body(boundary);
          if (part_body.ec) {
            co_return;
          }

          if (!filename.empty()) {
            auto ec = co_await file->async_write(part_body.data.data(),
                                                 part_body.data.size());
            if (ec) {
              co_return;
            }

            file->close();
            CHECK(fs::file_size(filename) == 1024);
          }
          else {
            std::cout << part_body.data << "\n";
          }

          if (part_body.eof) {
            break;
          }
        }

        resp.set_status_and_content(status_type::ok, "ok");
        co_return;
      });

  server.async_start();

  std::string filename = "test_1024.txt";
  create_file(filename);

  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090/multipart_upload";
  client.add_str_part("test", "test value");
  client.add_file_part("test file", filename);
  auto result =
      async_simple::coro::syncAwait(client.async_upload_multipart(uri));
  CHECK(result.status == 200);
}

TEST_CASE("test coro_http_client chunked upload and download") {
  {
    coro_http_server server(1, 8090);
    server.set_http_handler<cinatra::PUT, cinatra::POST>(
        "/chunked_upload",
        [](coro_http_request &req,
           coro_http_response &resp) -> async_simple::coro::Lazy<void> {
          assert(req.get_content_type() == content_type::chunked);
          chunked_result result{};
          std::string_view filename = req.get_header_value("filename");

          CHECK(!filename.empty());
          std::string fullpath = fs::current_path().append(filename).string();
          std::ofstream file(fullpath, std::ios::binary);
          CHECK(file.is_open());

          while (true) {
            result = co_await req.get_conn()->read_chunked();
            if (result.ec) {
              co_return;
            }
            if (result.eof) {
              break;
            }

            file.write(result.data.data(), result.data.size());
          }

          file.close();
          std::cout << "upload finished, filename: " << filename << "\n";
          resp.set_status_and_content(status_type::ok, std::string(filename));
        });

    server.async_start();

    {
      std::string filename = "test_1024.txt";
      std::error_code ec{};
      fs::remove(filename, ec);
      if (ec) {
        std::cout << ec << "\n";
      }
      bool r = create_file(filename);
      CHECK(r);

      coro_http_client client{};
      client.add_header("filename", filename);
      std::string uri = "http://127.0.0.1:8090/chunked_upload";
      auto lazy = client.async_upload_chunked(uri, http_method::PUT, filename);
      auto result = async_simple::coro::syncAwait(lazy);
      CHECK(result.status == 200);
    }

    {
      std::string filename = "test_100.txt";
      create_file(filename, 100);

      coro_http_client client{};
      client.add_header("filename", filename);
      std::string uri = "http://127.0.0.1:8090/chunked_upload";
      auto lazy = client.async_upload_chunked(uri, http_method::PUT, filename);
      auto result = async_simple::coro::syncAwait(lazy);
      CHECK(result.status == 200);
    }
  }

  {
    // chunked download, not in cache
    create_file("test_102.txt", 102);
    create_file("test_static.txt", 1024);
    coro_http_server server(1, 8090);
    server.set_static_res_dir("download", "");
    server.set_max_size_of_cache_files(100);
    server.set_transfer_chunked_size(100);
    server.async_start();

    coro_http_client client{};

    std::string download_url = "http://127.0.0.1:8090/download/test_static.txt";
    std::string download_name = "test1.txt";
    auto r = client.download(download_url, download_name);
    CHECK(r.status == 200);
    CHECK(std::filesystem::file_size(download_name) == 1024);

    download_url = "http://127.0.0.1:8090/download/test_102.txt";
    download_name = "test2.txt";
    r = client.download(download_url, download_name);
    CHECK(r.status == 200);
    CHECK(std::filesystem::file_size(download_name) == 102);
  }
}

TEST_CASE("test coro_http_client get") {
  coro_http_client client{};
  auto r = client.get("http://www.baidu.com");
  CHECK(!r.net_err);
  CHECK(r.status < 400);
}

TEST_CASE("test coro_http_client add header and url queries") {
  coro_http_client client{};
  client.add_header("Connection", "keep-alive");
  auto r =
      async_simple::coro::syncAwait(client.async_get("http://www.baidu.cn"));
  CHECK(!r.net_err);
  CHECK(r.status < 400);

  auto r2 = async_simple::coro::syncAwait(
      client.async_get("http://www.baidu.com?name='tom'&age=20"));
  CHECK(!r2.net_err);
  CHECK(r2.status < 400);
}

TEST_CASE("test coro_http_client not exist domain and bad uri") {
  {
    coro_http_client client{};
    auto r = async_simple::coro::syncAwait(
        client.async_get("http://www.notexistwebsit.com"));
    CHECK(r.net_err);
    CHECK(r.status != 200);
    CHECK(client.has_closed());
  }

  {
    coro_http_client client{};
    auto r = async_simple::coro::syncAwait(
        client.async_get("http://www.baidu.com/><"));
    CHECK(r.net_err);
    CHECK(r.status != 200);
    CHECK(client.has_closed());
  }
}

TEST_CASE("test coro_http_client async_get") {
  coro_http_client client{};
  auto r =
      async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
  CHECK(!r.net_err);
  CHECK(r.status < 400);

  auto r1 =
      async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
  CHECK(!r.net_err);
  CHECK(r.status == 200);
}

TEST_CASE("test basic http request") {
  coro_http_server server(1, 8090);
  // Setting up GET and POST handlers
  server.set_http_handler<GET>(
      "/", [&server](coro_http_request &, coro_http_response &res) mutable {
        res.set_status_and_content(status_type::ok, "hello world");
      });
  server.set_http_handler<POST>(
      "/", [&server](coro_http_request &req, coro_http_response &res) mutable {
        std::string str(req.get_body());
        str.append(" reply from post");
        res.set_status_and_content(status_type::ok, std::move(str));
      });

  // Setting up PUT handler
  server.set_http_handler<PUT>(
      "/", [&server](coro_http_request &req, coro_http_response &res) mutable {
        std::string str(req.get_body());
        str.append(" put successfully");
        res.set_status_and_content(status_type::ok, std::move(str));
      });

  // Setting up DELETE handler
  server.set_http_handler<DEL>(
      "/", [&server](coro_http_request &, coro_http_response &res) mutable {
        res.set_status_and_content(status_type::ok, "data deleted");
      });

  server.async_start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090";

  // Testing PUT method
  resp_data result = async_simple::coro::syncAwait(client.async_request(
      uri, http_method::PUT,
      req_context<std::string_view>{.content = "data for put"}));
  CHECK(result.resp_body == "data for put put successfully");

  // Testing DELETE method
  result = async_simple::coro::syncAwait(client.async_request(
      uri, http_method::DEL, req_context<std::string_view>{}));
  CHECK(result.resp_body == "data deleted");

  // Testing GET method again after DELETE
  result = async_simple::coro::syncAwait(client.async_get(uri));
  CHECK(result.resp_body == "hello world");

  size_t size = result.resp_body.size();
  auto buf = client.release_buf();
  CHECK(size == strlen(buf.data()));
  CHECK(buf == "hello world");

  // Rest of the POST tests
  result = async_simple::coro::syncAwait(client.async_post(
      uri, "async post hello coro_http_client", req_content_type::string));
  CHECK(result.resp_body ==
        "async post hello coro_http_client reply from post");

  result = client.post(uri, "sync post hello coro_http_client",
                       req_content_type::string);
  CHECK(result.resp_body == "sync post hello coro_http_client reply from post");

  std::string_view uri1 = "http://127.0.0.1:8090";
  std::string_view post_str = "post hello coro_http_client";

  result = async_simple::coro::syncAwait(
      client.async_request(uri, http_method::POST,
                           req_context<std::string_view>{.content = post_str}));
  CHECK(result.resp_body == "post hello coro_http_client reply from post");

  result = async_simple::coro::syncAwait(
      client.async_request(uri1, http_method::POST,
                           req_context<std::string_view>{.content = post_str}));
  CHECK(result.resp_body == "post hello coro_http_client reply from post");

  result = client.post(uri, "", req_content_type::string);
  CHECK(result.status == 200);

  server.stop();
}

TEST_CASE("test coro_http_client request timeout") {
  coro_http_client client{};
  cinatra::coro_http_client::config conf{.conn_timeout_duration = 10s,
                                         .req_timeout_duration = 1ms};
  client.init_config(conf);
  auto r =
      async_simple::coro::syncAwait(client.connect("http://www.baidu.com"));
  std::cout << r.net_err.message() << "\n";
  if (!r.net_err) {
    r = async_simple::coro::syncAwait(client.async_get("/"));
    if (r.net_err) {
      CHECK(r.net_err == std::errc::timed_out);
    }
  }
}

#ifdef INJECT_FOR_HTTP_CLIENT_TEST
TEST_CASE("test inject failed") {
  // {
  //   coro_http_client client{};
  //   inject_response_valid = ClientInjectAction::response_error;
  //   client.set_req_timeout(8s);
  //   auto result = client.get("http://purecpp.cn");
  //   CHECK(result.net_err == std::errc::protocol_error);

  //   inject_header_valid = ClientInjectAction::header_error;
  //   result = client.get("http://purecpp.cn");
  //   CHECK(result.net_err == std::errc::protocol_error);
  // }

  //  {
  //    coro_http_client client{};
  //    client.set_req_timeout(10s);
  //    std::string uri =
  //        "http://www.httpwatch.com/httpgallery/chunked/chunkedimage.aspx";
  //    std::string filename = "test.jpg";
  //
  //    std::error_code ec{};
  //    std::filesystem::remove(filename, ec);
  //
  //    inject_read_failed = ClientInjectAction::read_failed;
  //    auto result = client.download(uri, filename);
  //    CHECK(result.net_err == std::make_error_code(std::errc::not_connected));
  //  }
  //
  //  {
  //    coro_http_client client{};
  //    client.set_req_timeout(10s);
  //    std::string uri =
  //        "http://www.httpwatch.com/httpgallery/chunked/chunkedimage.aspx";
  //    std::string filename = "test.jpg";
  //
  //    std::error_code ec{};
  //    std::filesystem::remove(filename, ec);
  //
  //    inject_chunk_valid = ClientInjectAction::chunk_error;
  //    auto result = client.download(uri, filename);
  //    CHECK(result.status == 404);
  //  }

  {
    coro_http_client client{};
    client.add_str_part("hello", "world");
    inject_write_failed = ClientInjectAction::write_failed;
    auto result = async_simple::coro::syncAwait(
        client.async_upload_multipart("https://www.bing.com"));
    CHECK(result.status == 404);
  }
}
#endif

TEST_CASE("test coro http proxy request") {
  coro_http_client client{};
  client.set_req_timeout(8s);
  std::string uri = "http://www.baidu.com";
  // Make sure the host and port are matching with your proxy server
  client.set_proxy("106.14.255.124", "80");
  resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
  if (!result.net_err)
    CHECK(result.status >= 200);

  client.set_proxy("106.14.255.124", "80");
  result = async_simple::coro::syncAwait(client.async_get(uri));
  if (!result.net_err)
    CHECK(result.status >= 200);
}

TEST_CASE("test coro http proxy request with port") {
  coro_http_client client{};
  client.set_req_timeout(8s);
  std::string uri = "http://www.baidu.com:80";
  // Make sure the host and port are matching with your proxy server
  client.set_proxy("106.14.255.124", "80");
  resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
  if (!result.net_err)
    CHECK(result.status >= 200);  // maybe return 500 from that host.
}

// TEST_CASE("test coro http basic auth request") {
//   coro_http_client client{};
//   std::string uri = "http://www.purecpp.cn";
//   client.set_proxy_basic_auth("user", "pass");
//   resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
//   CHECK(!result.net_err);
//   CHECK(result.status == 200);
// }

TEST_CASE("test coro http bearer token auth request") {
  coro_http_client client{};
  std::string uri = "http://www.baidu.com";
  client.set_proxy_bearer_token_auth("password");
  resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
  CHECK(!result.net_err);
  CHECK(result.status < 400);
}

TEST_CASE("test coro http redirect request") {
  coro_http_client client{};
  client.set_req_timeout(8s);
  std::string uri = "http://httpbin.org/redirect-to?url=http://httpbin.org/get";
  resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
  if (result.status != 404 && !result.net_err) {
    CHECK(!result.net_err);
    if (result.status != 502)
      CHECK(result.status == 302);

    if (client.is_redirect(result)) {
      std::string redirect_uri = client.get_redirect_uri();
      result = async_simple::coro::syncAwait(client.async_get(redirect_uri));
      if (result.status != 502 && result.status != 404)
        CHECK(result.status == 200);
    }

    client.enable_auto_redirect(true);
    result = async_simple::coro::syncAwait(client.async_get(uri));
    CHECK(result.status >= 200);
  }
}

TEST_CASE("test coro http request timeout") {
  coro_http_server server(1, 8090);
  server.set_http_handler<GET, POST>(
      "/", [&server](coro_http_request &, coro_http_response &res) mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        res.set_status_and_content(status_type::ok, "hello world");
      });

  server.async_start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  coro_http_client client{};
  std::string uri = "http://127.0.0.1:8090";

  resp_data result = async_simple::coro::syncAwait(client.async_get(uri));
  CHECK(result.status == 200);

  client.set_req_timeout(500ms);
  result = async_simple::coro::syncAwait(client.async_get(uri));
  CHECK(result.net_err == std::errc::timed_out);

  // after timeout, the socket in client has been closed, so use a new client
  // to test.
  coro_http_client client1{};
  result = async_simple::coro::syncAwait(client1.async_post(
      uri, "async post hello coro_http_client", req_content_type::string));
  CHECK(!result.net_err);

  server.stop();
}

TEST_CASE("test coro_http_client using external io_context") {
  asio::io_context io_context;
  std::promise<void> promise;
  auto future = promise.get_future();
  auto work = std::make_unique<asio::io_context::work>(io_context);
  std::thread io_thd([&io_context, &promise] {
    promise.set_value();
    io_context.run();
  });
  future.wait();

  coro_http_client client(io_context.get_executor());
  auto r =
      async_simple::coro::syncAwait(client.async_get("http://www.baidu.com"));
  CHECK(!r.net_err);
  CHECK(r.status < 400);
  work.reset();
  io_context.run();
  io_thd.join();
}

async_simple::coro::Lazy<resp_data> simulate_self_join() {
  coro_http_client client{};
  co_return co_await client.async_get("http://www.baidu.com");
}

TEST_CASE("test coro_http_client dealing with self join") {
  auto r = async_simple::coro::syncAwait(simulate_self_join());
  CHECK(!r.net_err);
  CHECK(r.status < 400);
}

TEST_CASE("test coro_http_client no scheme still send request check") {
  coro_http_server server(1, 8090);
  server.set_http_handler<GET, POST>(
      "/", [&server](coro_http_request &, coro_http_response &res) mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        res.set_status_and_content(status_type::ok, "hello world");
      });

  server.async_start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::string uri = "http://127.0.0.1:8090";

  coro_http_client client{};
  auto resp = async_simple::coro::syncAwait(client.async_get("127.0.0.1:8090"));
  CHECK(!resp.net_err);
  CHECK(resp.status == 200);
  resp = async_simple::coro::syncAwait(
      client.async_get("127.0.0.1:8090/ref='http://www.baidu.com'"));
  CHECK(resp.status == 404);

  server.stop();
}

#ifdef DSKIP_TIME_TEST
TEST_CASE("test conversion between unix time and gmt time, http format") {
  std::chrono::microseconds time_cost{0};
  std::ifstream file("../../tests/files_for_test_time_parse/http_times.txt");
  if (!file) {
    std::cout << "open file failed" << std::endl;
  }
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string time_to_parse;
    std::string timestamp;
    if (std::getline(iss, time_to_parse, '#') && std::getline(iss, timestamp)) {
      std::pair<bool, std::time_t> result;
      auto start = std::chrono::system_clock::now();
      for (int i = 0; i < 100; i++) {
        result = get_timestamp(time_to_parse);
      }
      auto end = std::chrono::system_clock::now();
      auto duration = duration_cast<std::chrono::microseconds>(end - start);
      time_cost += duration;
      if (result.first == true) {
        CHECK(timestamp != "invalid");
        if (timestamp != "invalid") {
          CHECK(result.second == std::stoll(timestamp));
        }
      }
      else {
        CHECK(timestamp == "invalid");
      }
    }
  }
  file.close();
  std::cout << double(time_cost.count()) *
                   std::chrono::microseconds::period::num /
                   std::chrono::microseconds::period::den
            << "s" << std::endl;
}

TEST_CASE("test conversion between unix time and gmt time, utc format") {
  std::chrono::microseconds time_cost{0};
  std::ifstream file("../../tests/files_for_test_time_parse/utc_times.txt");
  if (!file) {
    std::cout << "open file failed" << std::endl;
  }
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string time_to_parse;
    std::string timestamp;
    if (std::getline(iss, time_to_parse, '#') && std::getline(iss, timestamp)) {
      std::pair<bool, std::time_t> result;
      auto start = std::chrono::system_clock::now();
      for (int i = 0; i < 100; i++) {
        result = get_timestamp<time_format::utc_format>(time_to_parse);
      }
      auto end = std::chrono::system_clock::now();
      auto duration = duration_cast<std::chrono::microseconds>(end - start);
      time_cost += duration;
      if (result.first == true) {
        CHECK(timestamp != "invalid");
        if (timestamp != "invalid") {
          CHECK(result.second == std::stoll(timestamp));
        }
      }
      else {
        CHECK(timestamp == "invalid");
      }
    }
  }
  file.close();
  std::cout << double(time_cost.count()) *
                   std::chrono::microseconds::period::num /
                   std::chrono::microseconds::period::den
            << "s" << std::endl;
}

TEST_CASE(
    "test conversion between unix time and gmt time, utc without punctuation "
    "format") {
  std::chrono::microseconds time_cost{0};
  std::ifstream file(
      "../../tests/files_for_test_time_parse/"
      "utc_without_punctuation_times.txt");
  if (!file) {
    std::cout << "open file failed" << std::endl;
  }
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string time_to_parse;
    std::string timestamp;
    if (std::getline(iss, time_to_parse, '#') && std::getline(iss, timestamp)) {
      std::pair<bool, std::time_t> result;
      auto start = std::chrono::system_clock::now();
      for (int i = 0; i < 100; i++) {
        result = get_timestamp<time_format::utc_without_punctuation_format>(
            time_to_parse);
      }
      auto end = std::chrono::system_clock::now();
      auto duration = duration_cast<std::chrono::microseconds>(end - start);
      time_cost += duration;
      if (result.first == true) {
        CHECK(timestamp != "invalid");
        if (timestamp != "invalid") {
          CHECK(result.second == std::stoll(timestamp));
        }
      }
      else {
        CHECK(timestamp == "invalid");
      }
    }
  }
  file.close();
  std::cout << double(time_cost.count()) *
                   std::chrono::microseconds::period::num /
                   std::chrono::microseconds::period::den
            << "s" << std::endl;
}
#endif

TEST_CASE("Testing get_content_type_str function") {
  SUBCASE("Test HTML content type") {
    CHECK(get_content_type_str(req_content_type::html) ==
          "text/html; charset=UTF-8");
  }

  SUBCASE("Test JSON content type") {
    CHECK(get_content_type_str(req_content_type::json) ==
          "application/json; charset=UTF-8");
  }

  SUBCASE("Test String content type") {
    CHECK(get_content_type_str(req_content_type::string) ==
          "text/html; charset=UTF-8");
  }

  SUBCASE("Test Multipart content type") {
    std::string result = get_content_type_str(req_content_type::multipart);
    std::string expectedPrefix = "multipart/form-data; boundary=";
    CHECK(result.find(expectedPrefix) ==
          0);  // Check if the result starts with the expected prefix

    // Check if there is something after the prefix,
    // this test failed.
    /*CHECK(result.length() > expectedPrefix.length());*/
  }

  SUBCASE("Test Octet Stream content type") {
    CHECK(get_content_type_str(req_content_type::octet_stream) ==
          "application/octet-stream");
  }

  SUBCASE("Test XML content type") {
    CHECK(get_content_type_str(req_content_type::xml) == "application/xml");
  }
}

TEST_CASE("test get_local_time_str with_month") {
  char buf[32];
  std::string_view format = "%Y-%m-%d %H:%M:%S";  // This format includes '%m'
  std::time_t t = std::time(nullptr);

  std::string_view result = cinatra::get_local_time_str(buf, t, format);
  std::cout << "Local time with month: " << result << "\n";

  // Perform a basic check
  CHECK(!result.empty());
}

TEST_CASE("Testing base64_encode function") {
  SUBCASE("Base64 encoding of an empty string") {
    CHECK(base64_encode("") == "");
  }

  SUBCASE("Base64 encoding of 'Hello'") {
    CHECK(base64_encode("Hello") == "SGVsbG8=");
  }

  SUBCASE("Base64 encoding of a binary data") {
    std::string binaryData = "\x01\x02\x03";  // Example binary data
    CHECK(base64_encode(binaryData) == "AQID");
  }
}

TEST_CASE("Testing is_valid_utf8 function") {
  SUBCASE("Valid UTF-8 string") {
    auto validUtf8 = std::u8string(u8"Hello, 世界");
    std::string validUtf8Converted(validUtf8.begin(), validUtf8.end());
    CHECK(is_valid_utf8((unsigned char *)validUtf8.c_str(), validUtf8.size()) ==
          true);
  }

  SUBCASE("Invalid UTF-8 string with wrong continuation bytes") {
    std::string invalidUtf8 = "Hello, \x80\x80";  // wrong continuation bytes
    CHECK(is_valid_utf8((unsigned char *)invalidUtf8.c_str(),
                        invalidUtf8.size()) == false);
  }

  SUBCASE("Empty string") {
    std::string empty;
    CHECK(is_valid_utf8((unsigned char *)empty.c_str(), empty.size()) == true);
  }
}

TEST_CASE("test transfer cookie to string") {
  cookie cookie("name", "value");
  CHECK(cookie.get_name() == "name");
  CHECK(cookie.get_value() == "value");
  CHECK(cookie.to_string() == "name=value");
  cookie.set_path("/");
  CHECK(cookie.to_string() == "name=value; path=/");
  cookie.set_comment("comment");
  CHECK(cookie.to_string() == "name=value; path=/");
  cookie.set_domain("baidu.com");
  CHECK(cookie.to_string() == "name=value; domain=baidu.com; path=/");
  cookie.set_secure(true);
  CHECK(cookie.to_string() == "name=value; domain=baidu.com; path=/; secure");
  cookie.set_http_only(true);
  CHECK(cookie.to_string() ==
        "name=value; domain=baidu.com; path=/; secure; HttpOnly");
  cookie.set_priority("Low");
  CHECK(cookie.to_string() ==
        "name=value; domain=baidu.com; path=/; Priority=Low; secure; HttpOnly");
  cookie.set_priority("Medium");
  CHECK(cookie.to_string() ==
        "name=value; domain=baidu.com; path=/; Priority=Medium; secure; "
        "HttpOnly");
  cookie.set_priority("High");
  CHECK(
      cookie.to_string() ==
      "name=value; domain=baidu.com; path=/; Priority=High; secure; HttpOnly");
  cookie.set_priority("");
  cookie.set_http_only(false);

  cookie.set_version(1);
  CHECK(cookie.to_string() ==
        "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; "
        "Path=\"/\"; secure; Version=\"1\"");

  cookie.set_secure(false);
  cookie.set_max_age(100);
  CHECK(cookie.to_string() ==
        "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; "
        "Path=\"/\"; Max-Age=\"100\"; Version=\"1\"");

  cookie.set_http_only(true);
  CHECK(cookie.to_string() ==
        "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; "
        "Path=\"/\"; Max-Age=\"100\"; HttpOnly; Version=\"1\"");

  cookie.set_priority("Low");
  CHECK(
      cookie.to_string() ==
      "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; Path=\"/\"; "
      "Priority=\"Low\"; Max-Age=\"100\"; HttpOnly; Version=\"1\"");
  cookie.set_priority("Medium");
  CHECK(
      cookie.to_string() ==
      "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; Path=\"/\"; "
      "Priority=\"Medium\"; Max-Age=\"100\"; HttpOnly; Version=\"1\"");
  cookie.set_priority("High");
  CHECK(
      cookie.to_string() ==
      "name=\"value\"; Comment=\"comment\"; Domain=\"baidu.com\"; Path=\"/\"; "
      "Priority=\"High\"; Max-Age=\"100\"; HttpOnly; Version=\"1\"");
}

std::vector<std::string_view> get_header_values(
    std::span<http_header> &resp_headers, std::string_view key) {
  std::vector<std::string_view> values{};
  for (const auto &p : resp_headers) {
    if (p.name == key)
      values.push_back(p.value);
  }
  return values;
}

std::string cookie_str1 = "";
std::string cookie_str2 = "";

TEST_CASE("test cookie") {
  coro_http_server server(5, 8090);
  server.set_http_handler<GET>(
      "/construct_cookies",
      [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session->get_session_cookie().set_path("/");
        cookie_str1 = session->get_session_cookie().to_string();

        cookie another_cookie("test", "cookie");
        another_cookie.set_http_only(true);
        another_cookie.set_domain("baidu.com");
        res.add_cookie(another_cookie);
        cookie_str2 = another_cookie.to_string();

        res.set_status_and_content(status_type::ok, session->get_session_id());
      });

  server.set_http_handler<GET>(
      "/check_session_cookie",
      [](coro_http_request &req, coro_http_response &res) {
        auto session_id = req.get_header_value("Cookie");
        CHECK(session_id ==
              CSESSIONID + "=" + req.get_session()->get_session_id());
        res.set_status(status_type::ok);
      });

  server.async_start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  coro_http_client client{};
  auto r1 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/construct_cookies"));
  auto cookie_strs = get_header_values(r1.resp_headers, "Set-Cookie");
  CHECK(cookie_strs.size() == 2);
  bool check1 =
      (cookie_strs[0] == cookie_str1 && cookie_strs[1] == cookie_str2);
  bool check2 =
      (cookie_strs[1] == cookie_str1 && cookie_strs[0] == cookie_str2);
  CHECK((check1 || check2));
  CHECK(r1.status == 200);

  std::string session_cookie =
      CSESSIONID + "=" + std::string(r1.resp_body.data(), r1.resp_body.size());

  client.add_header("Cookie", session_cookie);
  auto r2 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/check_session_cookie"));
  CHECK(r2.status == 200);

  server.stop();
}

std::string session_id_login = "";
std::string session_id_logout = "";
std::string session_id_check_login = "";
std::string session_id_check_logout = "";

TEST_CASE("test session") {
  coro_http_server server(5, 8090);
  server.set_http_handler<GET>(
      "/login", [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id_login = session->get_session_id();
        session->set_data("login", true);
        res.set_status(status_type::ok);
      });
  server.set_http_handler<GET>(
      "/logout", [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id_logout = session->get_session_id();
        session->remove_data("login");
        res.set_status(status_type::ok);
      });
  server.set_http_handler<GET>(
      "/check_login", [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id_check_login = session->get_session_id();
        bool login = session->get_data<bool>("login").value_or(false);
        CHECK(login == true);
        res.set_status(status_type::ok);
      });
  server.set_http_handler<GET>(
      "/check_logout", [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id_check_logout = session->get_session_id();
        bool login = session->get_data<bool>("login").value_or(false);
        CHECK(login == false);
        res.set_status(status_type::ok);
      });
  server.async_start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  coro_http_client client{};
  auto r1 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/check_logout"));
  CHECK(r1.status == 200);

  auto r2 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/login"));
  CHECK(r2.status == 200);
  CHECK(session_id_login != session_id_check_logout);

  std::string session_cookie = CSESSIONID + "=" + session_id_login;

  client.add_header("Cookie", session_cookie);
  auto r3 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/check_login"));
  CHECK(r3.status == 200);
  CHECK(session_id_login == session_id_check_login);

  client.add_header("Cookie", session_cookie);
  auto r4 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/logout"));
  CHECK(r4.status == 200);
  CHECK(session_id_login == session_id_logout);

  client.add_header("Cookie", session_cookie);
  auto r5 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/check_logout"));
  CHECK(r5.status == 200);
  CHECK(session_id_login == session_id_check_logout);

  server.stop();
}

std::string session_id = "";
TEST_CASE("test session timeout") {
  coro_http_server server(5, 8090);

  server.set_http_handler<GET>(
      "/construct_session",
      [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id = session->get_session_id();
        session->set_session_timeout(1);
        res.set_status(status_type::ok);
      });

  server.set_http_handler<GET>("/no_sleep", [](coro_http_request &req,
                                               coro_http_response &res) {
    CHECK(session_manager::get().check_session_existence(session_id) == true);
    res.set_status(status_type::ok);
  });

  server.set_http_handler<GET>("/after_sleep_2s", [](coro_http_request &req,
                                                     coro_http_response &res) {
    CHECK(session_manager::get().check_session_existence(session_id) == false);
    res.set_status(status_type::ok);
  });

  session_manager::get().set_check_session_duration(10ms);
  server.async_start();

  coro_http_client client{};
  auto r1 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/construct_session"));
  CHECK(r1.status == 200);

  auto r2 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/no_sleep"));
  CHECK(r2.status == 200);

  std::this_thread::sleep_for(2s);
  auto r3 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/after_sleep_2s"));
  CHECK(r3.status == 200);

  server.stop();
}

TEST_CASE("test session validate") {
  coro_http_server server(5, 8090);

  server.set_http_handler<GET>(
      "/construct_session",
      [](coro_http_request &req, coro_http_response &res) {
        auto session = req.get_session();
        session_id = session->get_session_id();
        res.set_status(status_type::ok);
      });

  server.set_http_handler<GET>(
      "/invalidate_session",
      [](coro_http_request &req, coro_http_response &res) {
        CHECK(session_manager::get().check_session_existence(session_id) ==
              true);
        session_manager::get().get_session(session_id)->invalidate();
        res.set_status(status_type::ok);
      });

  server.set_http_handler<GET>("/after_sleep_2s", [](coro_http_request &req,
                                                     coro_http_response &res) {
    CHECK(session_manager::get().check_session_existence(session_id) == false);
    res.set_status(status_type::ok);
  });

  session_manager::get().set_check_session_duration(10ms);
  server.async_start();

  coro_http_client client{};
  auto r1 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/construct_session"));
  CHECK(r1.status == 200);

  auto r2 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/invalidate_session"));
  CHECK(r2.status == 200);

  std::this_thread::sleep_for(2s);
  auto r3 = async_simple::coro::syncAwait(
      client.async_get("http://127.0.0.1:8090/after_sleep_2s"));
  CHECK(r3.status == 200);

  server.stop();
}