import net from 'node:net';
import fs from 'node:fs';
import {randomUUID} from 'node:crypto';
const endpointFile=process.argv[2];
const pipe='\\\\.\\pipe\\BrowserControl-sdk-test-'+randomUUID();
const token=randomUUID();let executed=0;
const server=net.createServer(socket=>{
 let data=Buffer.alloc(0);socket.on('error',()=>{});
 socket.on('data',chunk=>{
  data=Buffer.concat([data,chunk]);if(data.length<4)return;
  const length=data.readUInt32LE(0);if(data.length<length+4)return;
  socket.removeAllListeners('data');
  const request=JSON.parse(data.subarray(4,4+length).toString('utf8'));
  if(request.token!==token||request.version!==1){socket.destroy();return;}
  if(request.method==='fixture.hang'){return;} if(request.method==='fixture.oversize'){const huge=Buffer.alloc(4);huge.writeUInt32LE(64*1024*1024+1);socket.end(huge);return;} if(request.method==='fixture.drop'){executed++;socket.end();return;}
  const reply={id:request.method==='fixture.bad_id'?'wrong':request.id,ok:true,result:{ready:true,title:'caf\u00e9',method:request.method,parameters:request.params}};
  if(request.method==='tabs.create')reply.result={id:'test-tab'};
  if(request.method==='fixture.count')reply.result=executed; if(request.method==='fixture.raw_uint64')reply.result=data.subarray(4,4+length).toString('utf8').includes('18446744073709551615'); if(request.method==='fixture.uint64')reply.result='UNSIGNED64_TEST';
  if(request.method==='fixture.error'){reply.ok=false;delete reply.result;reply.error={code:'test_error',message:'Synthetic browser error',details:{sample:1}};}
  const body=Buffer.from(JSON.stringify(reply).replace('"UNSIGNED64_TEST"','18446744073709551615'));const header=Buffer.alloc(4);header.writeUInt32LE(body.length);
  socket.write(header.subarray(0,2));setTimeout(()=>socket.end(Buffer.concat([header.subarray(2),body])),5);
 });
});
server.listen(pipe,()=>{fs.writeFileSync(endpointFile,JSON.stringify({version:1,pipe,token}));console.log('READY');});
process.stdin.resume();process.stdin.on('data',()=>server.close(()=>process.exit(0)));
