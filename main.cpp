#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

using uint128_t = __uint128_t;

std::string
format_address(uint128_t a) {
  return std::format("{:016x}{:016x}",
                     static_cast<std::uint64_t>(a >> 64),
                     static_cast<std::uint64_t>(a));
}

uint128_t
parse_address(std::string const& str) {
  std::uint8_t buf[sizeof(in6_addr)];
  inet_pton(AF_INET6, str.c_str(), buf);

  uint128_t result{};
  for (std::size_t i = 0; i < sizeof(in6_addr); ++i)
    result = (result << 8) | buf[i];

  return result;
}

std::string
address_to_string(uint128_t a) {
  std::uint8_t address_buf[sizeof(in6_addr)]{};
  for (std::size_t i = 0; i < sizeof(in6_addr); ++i) {
    address_buf[sizeof(in6_addr) - i - 1] = a;
    a >>= 8;
  }

  char result_buf[INET6_ADDRSTRLEN];
  return inet_ntop(AF_INET6, address_buf, result_buf, INET6_ADDRSTRLEN);
}

struct address_prefix {
  static constexpr uint128_t highest_bit = static_cast<uint128_t>(1) << 127;

  uint128_t address;
  unsigned  prefix_length;

  std::optional<unsigned>
  pop_bit() {
    if (prefix_length-- == 0)
      return std::nullopt;

    bool bit = address & highest_bit;
    address <<= 1;
    return bit;
  }
};

std::string
address_prefix_to_string(address_prefix p) {
  return address_to_string(p.address) + "/" + std::to_string(p.prefix_length);
}

struct prefix_and_pop {
  address_prefix prefix;
  std::uint16_t  pop;
};

struct pop_and_prefix_length {
  std::uint16_t pop;
  unsigned      prefix_length;
};

struct routing_trie {
  struct node {
    std::optional<std::uint16_t> pop;
    std::unique_ptr<node>        children[2];
  };

  node root;

  void
  insert(prefix_and_pop e) {
    address_prefix prefix = e.prefix;

    node* current = &root;
    for (auto bit = prefix.pop_bit(); bit; bit = prefix.pop_bit()) {
      if (!current->children[*bit])
        current->children[*bit] = std::make_unique<node>();
      current = current->children[*bit].get();
    }

    if (current->pop) {
      std::cerr << "Duplicate prefix: " << address_to_string(e.prefix.address)
                << "/" << e.prefix.prefix_length
                << '\n';
      std::abort();
    }

    current->pop = e.pop;
  }

  std::optional<pop_and_prefix_length>
  find(address_prefix prefix) const {
    std::optional<pop_and_prefix_length> best;
    unsigned depth = 0;

    node const* current = &root;
    for (auto bit = prefix.pop_bit(); bit && current; bit = prefix.pop_bit()) {
      ++depth;

      current = current->children[*bit].get();
      if (current && current->pop)
        best = {*current->pop, depth};
    }

    while (current && !best) {
      if (current->pop) {
        best = {*current->pop, depth};
        break;
      }

      ++depth;

      if (current->children[0])
        current = current->children[0].get();
      else
        current = current->children[1].get();
    }

    return best;
  }
};

address_prefix
parse_prefix(std::string const& line) {
  auto slash = line.find('/');
  if (slash == std::string::npos) {
    std::cerr << "Bad prefix format: " << line << '\n';
    std::abort();
  }

  auto address = parse_address(line.substr(0, slash));
  auto prefix_length = std::stoi(line.substr(slash + 1));
  return {address, static_cast<unsigned>(prefix_length)};
}

prefix_and_pop
parse_entry(std::string const& line) {
  auto slash = line.find('/');
  if (slash == std::string::npos) {
    std::cerr << "Bad entry format: " << line << '\n';
    std::abort();
  }

  auto address = parse_address(line.substr(0, slash));

  auto space = line.find(' ', slash);
  if (space == std::string::npos) {
    std::cerr << "Bad entry format: " << line << '\n';
    std::abort();
  }

  auto prefix_length = std::stoi(line.substr(slash + 1, space));
  auto pop = std::stoi(line.substr(space + 1));

  return {{address, static_cast<unsigned>(prefix_length)},
          static_cast<std::uint16_t>(pop)};
}

routing_trie
parse_data() {
  std::ifstream in{"routing-data.txt"};
  std::string line;
  routing_trie t;
  while (std::getline(in, line))
    t.insert(parse_entry(line));
  return t;
}

void
process_route(routing_trie const& t, address_prefix ecs) {
  if (auto entry = t.find(ecs))
    std::cout << address_prefix_to_string(ecs)
              << " => PoP: "
              << entry->pop
              << ", prefix-length: "
              << entry->prefix_length
              << '\n';
  else
    std::cout << address_prefix_to_string(ecs)
              << " => no matching entry\n";
}

int
main() {
  routing_trie t = parse_data();

  std::string line;
  while (std::getline(std::cin, line))
    process_route(t, parse_prefix(line));
}
