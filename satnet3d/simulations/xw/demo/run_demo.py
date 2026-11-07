"""Run the 18x10, 10-second BE virtual simulation and verify its packet results."""
from pathlib import Path
import argparse,csv,json,math,os,re,shutil,statistics,subprocess,time

HERE=Path(__file__).resolve().parent
XW=HERE.parent
PROJECT=XW.parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=HERE/'output')
    parser.add_argument('--inet-root',type=Path)
    parser.add_argument('--library',type=Path,default=PROJECT/'src/libsatnet3d.so')
    parser.add_argument('--opp-run',default='opp_run')
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    executable=shutil.which(args.opp_run)
    if not executable: parser.error('Load the OMNeT++ environment so opp_run is available, or pass --opp-run.')
    candidates=([args.inet_root] if args.inet_root else [])+([Path(os.environ['INET_ROOT'])] if 'INET_ROOT' in os.environ else [])+[PROJECT.parent/'inet4.6',PROJECT.parent.parent/'inet4.6']
    inet=next((p.resolve() for p in candidates if (p/'src/libINET.so').is_file()),None)
    if inet is None: parser.error('Pass --inet-root pointing to a built INET 4.6 project.')
    library=args.library.resolve()
    if not library.is_file():parser.error('Build satnet3d with make makefiles && make, or pass --library.')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cmd=[executable,'-u','Cmdenv','-l',str(library),'-l',str(inet/'src/libINET.so'),'-n',f'{PROJECT/"simulations"}:{PROJECT/"src"}:{inet/"src"}','-f','demo/demo.ini','-c','BE-Demo','--result-dir='+str(out)]
    started=time.perf_counter()
    with (out/'simulation.log').open('w') as f: result=subprocess.run(cmd,cwd=XW,stdout=f,stderr=subprocess.STDOUT)
    elapsed=time.perf_counter()-started
    if result.returncode:
        print((out/'simulation.log').read_text()[-6000:]);raise SystemExit(result.returncode)
    with (out/'flow1.csv').open(newline='') as f: packets=list(csv.DictReader(f))
    if not packets:raise RuntimeError('No flow1 packets delivered')
    delays=[float(p['endToEndDelay'])*1000 for p in packets]
    sca=max(out.glob('BE-Demo-*.sca'),key=lambda p:p.stat().st_mtime)
    generated=None; satellites=set(); actual={}
    for line in sca.open():
        m=re.match(r'^scalar\s+\S+\.stations\[0\]\.app\[0\]\.io\s+packetSent:count\s+(\d+)',line)
        if m: generated=int(m.group(1))
        m=re.match(r'^par\s+\S+\.satellite\[(\d+)\]\s',line)
        if m: satellites.add(int(m.group(1)))
        m=re.match(r'^par\s+\S+\.walkerDeltaScenarioManager\s+(numPlanes|satsPerPlane)\s+(\d+)',line)
        if m: actual[m.group(1)]=int(m.group(2))
    assert actual=={'numPlanes':18,'satsPerPlane':10},actual
    assert satellites==set(range(180)),len(satellites)
    if generated is None:raise RuntimeError('Missing flow1 source packetSent:count in SCA')
    summary=dict(config='BE-Demo',simulation_time_s=10,num_planes=18,sats_per_plane=10,satellite_count=180,flow=1,production_interval_s=1,packet_length_bytes=1478,pcp=1,vlan_id=1,generated_packets=generated,received_packets=len(packets),unique_received_packets=len({p['sequenceNumber'] for p in packets}),min_delay_ms=min(delays),max_delay_ms=max(delays),mean_delay_ms=statistics.mean(delays),delay_range_ms=max(delays)-min(delays))
    assert all(int(p['priority'])==1 and int(p['vlanId'])==1 and int(p['packetBytes'])==1478 for p in packets)
    (out/'summary.json').write_text(json.dumps(summary,indent=2))
    if args.verify:
        expected=json.loads((HERE/'expected/summary.json').read_text())
        assert summary.keys()==expected.keys()
        for key,value in expected.items():
            if isinstance(value,str):assert summary[key]==value,key
            else:assert math.isclose(summary[key],value,rel_tol=1e-9,abs_tol=1e-6),(key,summary[key],value)
        with (HERE/'expected/flow1.csv').open(newline='') as f: ref=list(csv.DictReader(f))
        assert len(ref)==len(packets)
        for actual,wanted in zip(packets,ref):
            for key in wanted:
                assert math.isclose(float(actual[key]),float(wanted[key]),rel_tol=1e-9,abs_tol=1e-9),(key,actual[key],wanted[key])
        print('PASS: BE virtual simulation matches expected packet records and summary.')
    (out/'run_info.json').write_text(json.dumps(dict(elapsed_seconds=round(elapsed,4),verified=args.verify,command=cmd),indent=2))
    print(json.dumps(summary));print(f'Simulation wall time: {elapsed:.3f}s; results: {out}')

if __name__=='__main__':main()
