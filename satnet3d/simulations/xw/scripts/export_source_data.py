"""Export numeric source data for the supplied figures, preserving plot definitions.
Run from any directory: python export_source_data.py
Raw records and archived numerical tables serve as reference inputs.
"""
from pathlib import Path
import argparse, csv, importlib.util, math, re, shutil, time, zipfile
import numpy as np
import pandas as pd

XW = Path(__file__).resolve().parents[1]
SCHEMES = [('Best effort','best_effort','be'),('Ground Gated','ground_gated','jrs-ter'),('Our Proposal','our_proposal','jrs')]

def module(name):
    spec=importlib.util.spec_from_file_location(name,XW/'scripts'/f'{name}.py')
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def source_count(folder,flow):
    # Ordinary, non-retransmitting sources; exactly reproduce unique counts
    # for 2TT and row counts for the historical 20TT arrival plots.
    seen=set(); rows=0; unique_bytes=0.0
    for chunk in pd.read_csv(folder/f'flow{flow}.csv',usecols=['sequenceNumber','packetBytes'],chunksize=200000):
        rows+=len(chunk)
        chunk=chunk.drop_duplicates('sequenceNumber')
        new=~chunk.sequenceNumber.isin(seen)
        unique_bytes+=float(chunk.loc[new,'packetBytes'].sum())
        seen.update(chunk.loc[new,'sequenceNumber'].tolist())
    return rows,len(seen),unique_bytes

def provenance(folder):
    sca=next(folder.glob('*.sca'))
    attrs={}
    with sca.open() as f:
        for line in f:
            if line.startswith('config '): break
            if line.startswith('run '): attrs['run_id']=line[4:].strip()
            if line.startswith('attr '):
                _,key,value=line.rstrip().split(' ',2); attrs[key]=value
    return attrs,sca

def expand_raw_files(expression):
    files=[]
    for item in expression.split(';'):
        if ' ... ' in item:
            match=re.fullmatch(r'(.*)/flow(\d+)\.csv \.\.\. flow(\d+)\.csv',item)
            if not match: raise ValueError(item)
            patterns=[f'{match.group(1)}/flow{flow}.csv' for flow in range(int(match.group(2)),int(match.group(3))+1)]
        elif '{10,11}' in item:
            patterns=[item.replace('{10,11}',str(flow)) for flow in [10,11]]
        else:
            patterns=[item]
        for pattern in patterns:
            matches=sorted(XW.glob(pattern))
            if not matches: raise FileNotFoundError(pattern)
            files.extend(str(p.relative_to(XW)) for p in matches)
    return ';'.join(dict.fromkeys(files))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=XW/'Source Data')
    parser.add_argument('--zip',type=Path)
    args=parser.parse_args()
    out=args.output.resolve(); (out/'2tt').mkdir(parents=True,exist_ok=True); (out/'20tt').mkdir(parents=True,exist_ok=True)
    readme=out/'README.md'
    archive=XW.parents[2]/'Source Data.zip'
    if not readme.exists() and archive.is_file():
        with zipfile.ZipFile(archive) as z:
            readme.write_bytes(z.read('Source Data/README.md'))
    started=time.perf_counter(); m2=module('draw_2tt'); m20=module('draw_20tt')
    manifest=[]; provenance_rows=[]; validations=[]
    def add(display,scenario,stem,source,raw,script,arguments,notes):
        files=[f'images/{scenario}/{stem}.{ext}' for ext in ('png','pdf','svg')] if not stem.endswith('.csv') else [f'images/{scenario}/{stem}']
        manifest.append(dict(display_id=display,scenario=scenario,
            output_files=';'.join(files),source_data_files=';'.join(f'Source Data/{p}' for p in source),
            raw_files=raw,script=f'scripts/{script}',arguments=arguments,definition=notes,
            provenance='Recorded source runs: Source Data/experimental_provenance.csv'))
    packets=m2.load_delay_packets_only()
    summary=m2.summarize_samples(packets)
    saved=pd.read_csv(XW/'images/2tt/delay_range_source.csv')
    assert len(summary)==len(saved)
    for column in ['sample_count','min_delay_ms','max_delay_ms','range_ms']:
        assert np.allclose(summary[column],saved[column],rtol=1e-10,atol=1e-9,equal_nan=True),column
    validations.append('2TT range/count/min/max recomputed from raw TS records agree with archived source table.')
    summary.to_csv(out/'2tt/ts_delay_statistics.csv',index=False)
    rates=pd.read_csv(XW/'images/2tt/flow_success_rate_source.csv')
    for name in ['flow_success_rate_source.csv','table_1_throughput.csv','table_2_rms.csv']:
        shutil.copy2(XW/'images/2tt'/name,out/'2tt'/name)
    throughput_basis=[]
    for label,stem,dirname in SCHEMES:
        folder=XW/'results'/f'{dirname}-2tt'
        generated,params=m2.scalar_metadata(folder)
        attrs,sca=provenance(folder)
        provenance_rows.append(dict(dataset=f'{dirname}-2tt',recorded_config=attrs.get('configname',''),recorded_run=attrs.get('run_id',''),recorded_datetime=attrs.get('datetime',''),recorded_seed=attrs.get('seedset',''),recorded_input=attrs.get('inifile',''),current_hil_entry={'be':'HIL-BE-2TT','jrs-ter':'HIL-JRS-TER-2TT','jrs':'HIL-JRS-2TT'}[dirname],sca=str(sca.relative_to(XW)),interpretation='Archived experiment identified by recorded SCA configuration, run ID and seed.'))
        for flow in [10,11]:
            raw=m2.read_flow(folder,flow)
            retry=raw.groupby('sequenceNumber')['attempt'].max().gt(1) if 'attempt' in raw else pd.Series(False,index=raw.sequenceNumber.unique())
            raw['raw_csv_row']=raw.index+2
            if 'outcome' in raw: raw=raw[raw.outcome.eq('delivered')]
            raw=raw.sort_values('simTime').drop_duplicates('sequenceNumber',keep='first').copy()
            raw['flow']=flow; raw['scheme']=label; raw['delay_ms']=raw.endToEndDelay*1000
            raw['first_send_time_derived_s']=raw.simTime-raw.endToEndDelay
            raw['observed_retransmission']=raw.sequenceNumber.map(retry).fillna(False)
            prop=packets[('Our Proposal',flow)]
            raw['within_plotted_x_limits']=raw.sequenceNumber.between(prop.sequenceNumber.min(),prop.sequenceNumber.max())
            name=f'2tt/delay_flow{flow}_{stem}.csv'; raw.to_csv(out/name,index=False)
            add(f'2TT_delay_flow{flow}_{stem}','2tt',f'delay_bar_flow{flow}_{stem}_with_retx',[name],f'results/{dirname}-2tt/flow{flow}.csv','draw_2tt.py','--delay-only','First delivered row per original; retry flag is any received attempt > 1; x limits follow Proposed sequence range.')
        for flow in range(1,12):
            if flow<10:
                _,unique,nbytes=source_count(folder,flow)
            else:
                unique=len(packets[(label,flow)])
                nbytes=unique*params[(flow,'packetLength')]/8
            row=rates[(rates.scheme==label)&(rates.flow==flow)].iloc[0]
            assert int(row.successfully_received_unique_original_packets)==unique,(label,flow)
            assert int(row.generated_original_packets)==generated[flow],(label,flow)
            throughput_basis.append(dict(scheme=label,flow=flow,generated_original_packets=generated[flow],delivered_unique_original_packets=unique,configured_packet_bits=params[(flow,'packetLength')],delivered_bits=nbytes*8,ideal_bits=generated[flow]*params[(flow,'packetLength')],measurement_duration_s=61.0))
    basis=pd.DataFrame(throughput_basis); basis.to_csv(out/'2tt/throughput_basis.csv',index=False)
    existing_tp=pd.read_csv(out/'2tt/table_1_throughput.csv')
    for label in existing_tp.scheme:
        b=basis[basis.scheme==label]; t=existing_tp[existing_tp.scheme==label].iloc[0]
        assert np.isclose(b.delivered_bits.sum()/61/1e9,t.actual_throughput_Gbps)
        assert np.isclose(b.ideal_bits.sum()/61/1e9,t.ideal_throughput_Gbps)
    existing_rms=pd.read_csv(out/'2tt/table_2_rms.csv')
    for _,r in existing_rms.iterrows():
        q=summary[(summary.scheme==r.scheme)&(summary.flow==r.flow)]
        assert np.isclose(q[q['sample']=='All delivered packets'].rms_jitter_ms.iloc[0],r.all_sample_rms_ms)
        assert np.isclose(q[q['sample']=='Never retransmitted'].rms_jitter_ms.iloc[0],r.filtered_sample_rms_ms)
    validations.append('2TT archived reception counts, generated counts, RMS values and throughput match recomputed raw/scalar evidence.')
    add('2TT_range','2tt','figure_4_jitter',['2tt/ts_delay_statistics.csv']+[f'2tt/delay_flow{f}_{s}.csv' for _,s,_ in SCHEMES for f in [10,11]],'results/*-2tt/flow{10,11}.csv','draw_2tt.py','--jitter-only','max-minus-min per scheme/flow/sample; all delivered versus no received attempt > 1.')
    add('2TT_success_rate','2tt','figure_5_success_rate',['2tt/flow_success_rate_source.csv'],'results/*-2tt/flow*.csv;results/*-2tt/*.sca','draw_2tt.py','','Unique delivered originals divided by recorded generated originals; observation ends at 61 s.')
    add('2TT_table_throughput','2tt','table_1_throughput.csv',['2tt/table_1_throughput.csv','2tt/throughput_basis.csv'],'results/*-2tt/flow*.csv;results/*-2tt/*.sca','draw_2tt.py','','Delivered unique-original application bits / 61 s.')
    add('2TT_table_rms','2tt','table_2_rms.csv',['2tt/table_2_rms.csv','2tt/ts_delay_statistics.csv'],'results/*-2tt/flow{10,11}.csv','draw_2tt.py','','Population standard deviation of delay, evaluated separately for each sample.')
    ranges=[]; arrivals=[]
    for label,stem,dirname in SCHEMES:
        folder=XW/'results'/f'{dirname}-20tt'; delivered=[]
        attrs,sca=provenance(folder)
        provenance_rows.append(dict(dataset=f'{dirname}-20tt',recorded_config=attrs.get('configname',''),recorded_run=attrs.get('run_id',''),recorded_datetime=attrs.get('datetime',''),recorded_seed=attrs.get('seedset',''),recorded_input=attrs.get('inifile',''),current_hil_entry={'be':'HIL-BE-20TT','jrs-ter':'HIL-JRS-TER-20TT','jrs':'HIL-JRS-20TT'}[dirname],sca=str(sca.relative_to(XW)),interpretation='Archived experiment identified by recorded SCA configuration, run ID and seed.'))
        counts=m20.read_ideal_packet_counts(str(folder),m20.ALL_FLOWS,generation_cutoff_s=m20.TS_GENERATION_CUTOFF_S)
        for flow in range(1,30):
            if flow<10:
                rows,unique,_=source_count(folder,flow)
                received=rows
            else:
                raw=pd.read_csv(folder/f'flow{flow}.csv'); raw['raw_csv_row']=raw.index+2
                accepted=raw[raw.outcome.eq('delivered')].copy() if 'outcome' in raw else raw.copy()
                accepted['flow']=flow; accepted['ts_flow_index']=flow-9; accepted['scheme']=label
                accepted['delay_ms']=accepted.endToEndDelay*1000
                accepted['first_send_time_derived_s']=accepted.simTime-accepted.endToEndDelay
                delivered.append(accepted)
                for sample,chosen in [('all_delivered_rows',accepted),('delivered_rows_attempt_1',accepted[accepted.attempt.eq(1)] if 'attempt' in accepted else accepted)]:
                    values=chosen.delay_ms.to_numpy(); n=len(values)
                    value=float(np.ptp(values)) if n>=2 else np.nan
                    wanted=m20.calculate_flow_delay_range(str(folder),flow,include_retransmitted=(sample=='all_delivered_rows'))
                    assert np.isclose(value,wanted,equal_nan=True)
                    ranges.append(dict(scheme=label,flow=flow,ts_flow_index=flow-9,sample=sample,sample_count=n,min_delay_ms=float(values.min()) if n else np.nan,max_delay_ms=float(values.max()) if n else np.nan,range_ms=value))
                selected=accepted[accepted.first_send_time_derived_s<=59.9+1e-6]
                received=selected.sequenceNumber.nunique()
            denom=int(counts[flow-1])
            arrivals.append(dict(scheme=label,flow=flow,generated_denominator=denom,received_numerator=int(received),unreceived_count=denom-int(received),success_rate_percent=100*received/denom if denom else np.nan,ts_generation_cutoff_s=59.9 if flow>=10 else np.nan,denominator_rule='SCA count capped by generation cutoff' if flow>=10 else 'SCA sender count',numerator_rule='unique delivered sequence, derived original time <=59.9s+1e-6s' if flow>=10 else 'all recorded CSV rows'))
        name=f'20tt/delay_distribution_{stem}.csv'; pd.concat(delivered,ignore_index=True).to_csv(out/name,index=False)
        add(f'20TT_delay_distribution_{stem}','20tt',f'boxplot_delay_{stem}_20tt',[name],f'results/{dirname}-20tt/flow10.csv ... flow29.csv','draw_20tt.py','','All delivered rows per TS flow; flow indices 1..20 mean flow10..29.')
        add(f'20TT_arrival_{stem}','20tt',f'arrival_rate_{stem}_20tt',['20tt/flow_arrival_source.csv'],f'results/{dirname}-20tt/flow*.csv;results/{dirname}-20tt/*.sca','draw_20tt.py','','Select matching scheme; TS originals generated through 59.9s, delivered by run end; non-TS numerator is CSV row count.')
    pd.DataFrame(ranges).to_csv(out/'20tt/ts_delay_range_source.csv',index=False)
    pd.DataFrame(arrivals).to_csv(out/'20tt/flow_arrival_source.csv',index=False)
    for include,stem in [(True,'figure_4_jitter_20tt'),(False,'figure_5_jitter_20tt')]:
        add('20TT_'+('range_all' if include else 'range_initial_attempt'),'20tt',stem,['20tt/ts_delay_range_source.csv']+[f'20tt/delay_distribution_{s}.csv' for _,s,_ in SCHEMES],'results/*-20tt/flow10.csv ... flow29.csv','draw_20tt.py','','20 per-flow ranges per scheme; '+('all delivered rows.' if include else 'delivered attempt==1 rows; filtering unit: received row.'))
    for row in manifest:
        row['raw_files']=expand_raw_files(row['raw_files'])
    pd.DataFrame(manifest).to_csv(out/'display_index.csv',index=False)
    pd.DataFrame(provenance_rows).to_csv(out/'experimental_provenance.csv',index=False)
    for message in validations:
        print(f'PASS: {message}')
    print('PASS: 20TT per-flow ranges agree with the plotting function for 120 combinations.')
    if args.zip:
        args.zip.parent.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(args.zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for p in sorted(out.rglob('*')):
                if p.is_file(): z.write(p,Path('Source Data')/p.relative_to(out))
    print(f'Exported {len(manifest)} display mappings to {out} in {time.perf_counter()-started:.2f}s')

if __name__=='__main__':
    main()
