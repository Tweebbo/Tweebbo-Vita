#include "../source/net.c"
#include <assert.h>
World W;
void store_save(const char *key,const char *value){assert(key&&value);}
int main(void){
  mutex_init(&N.mu);
  job_signal_init(&N.http_wake);
  strcpy(N.token,"fixture-original");strcpy(N.ep.host,"127.0.0.1");N.ep.port=18997;
  int id=net_http("POST","/api/owner-profile","{}",true);assert(id);
  const u8 packet[]={22,0};assert(q_push(&N.out,packet,sizeof packet));assert(q_push(&N.in,packet,sizeof packet));
  W.in_room=true;net_use_token("fixture-restored");
  assert(N.state==NET_CONNECTING&&!W.in_room&&!N.in.count&&!N.out.count);
  assert(!strcmp(N.token,"fixture-restored"));
  assert(!strcmp(N.http[0].token,"fixture-original")&&N.http[0].endpoint.port==18997);
  strcpy(N.ep.host,"192.168.1.2");N.ep.port=8096;
  assert(!strcmp(N.http[0].endpoint.host,"127.0.0.1")&&N.http[0].endpoint.port==18997);
  N.http[0].done=true;HttpResult result;assert(net_http_result(id,&result));
  assert(!N.http[0].queued&&!N.http[0].token[0]);
  puts("OK: restoring identity clears room packets; queued HTTP retains its original account and hotel.");
}
