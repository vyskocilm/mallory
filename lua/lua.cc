extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <cxxtools/jsondeserializer.h>

#include "agents.h"
#include "bios_agent.h"

std::map<std::string, double> cache;

int main (int argc, char** argv) {
  char buff[256];
  int error;
  std::string lua_code;

  bios_agent_t *client = bios_agent_new("ipc://@/malamute", argv[0]);

  // Read configuration
  cxxtools::JsonDeserializer json(std::cin);
  json.deserialize();
  const cxxtools::SerializationInfo *si = json.si();
  si->getMember("evaluation") >>= lua_code;
  std::vector<std::string> in;
  si->getMember("in") >>= in;
  std::string out;
  si->getMember("out") >>= out;
  // Subscribe to all streams
  for(auto it : in) {
    bios_agent_set_consumer(client, bios_get_stream_measurements(), it.c_str());
    printf("Registered to receive '%s'\n", it.c_str());
  }

  while(!zsys_interrupted) {
    ymsg_t *yn = bios_agent_recv(client);
    if(yn == NULL)
      continue;

    // Update cache with updated values
    std::string topic = bios_agent_subject(client);
    double value = ((double)ymsg_get_int32(yn, "value")) *
          pow(10.0, (double)ymsg_get_int32(yn, "scale" ));
    printf("Got message '%s' with value %lf\n", topic.c_str(), value);
    auto f = cache.find(topic);
    if(f != cache.end()) {
      f->second = value;
    } else {
      cache.insert(std::make_pair(topic, value));
    }
    ymsg_destroy(&yn);

    // Do we have everything?
    if(cache.size() != in.size())
      continue;

    // Compute
    lua_State *L = lua_open();
    for(auto it : cache) {
      std::string var = it.first;
      var[var.find('@')] = '_';
      printf("Setting variable '%s' to %lf\n", var.c_str(), it.second);
      lua_pushnumber(L, it.second);
      lua_setglobal(L, var.c_str());
    }
    error = luaL_loadbuffer(L, lua_code.c_str(), lua_code.length(), "line") ||
            lua_pcall(L, 0, 2, 0);

    if(lua_isnumber(L, -1))
        fprintf(stdout, "ALERT (%s = %lf), %s\n", out.c_str(), lua_tonumber(L, -1), lua_tostring(L, -2));
    if (error) {
      fprintf(stderr, "%s", lua_tostring(L, -1));
      lua_pop(L, 1);  /* pop error message from the stack */
    }
    lua_close(L);
  }
  bios_agent_destroy(&client);
  return 0;
}