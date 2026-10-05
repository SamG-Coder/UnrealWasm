"""Summarize retained compiler diagnostics for the current actual scene cook."""
import collections
import datetime
import json
import pathlib
import re

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'engine/artifacts/shader-format'
status=json.loads((out/'cook-result.json').read_text(encoding='utf-8-sig'))
started=datetime.datetime.fromisoformat(status['startedUtc'].replace('Z','+00:00')).timestamp()
categories=collections.Counter()
examples={}
count=successful=failed=0
for directory in (out/'jobs').iterdir():
    source=directory/'source.hlsl'
    if not source.exists() or source.stat().st_mtime<started:
        continue
    count+=1
    manifest=directory/'manifest.json'
    failure=directory/'failure.json'
    if manifest.exists():
        successful+=1
    elif failure.exists():
        failed+=1
        item=json.loads(failure.read_text(encoding='utf-8'))
        error=item['error']
        match=re.search(r'error\[([^\]]+)\]: ([^\n]+)',error)
        category=': '.join(match.groups()) if match else error.splitlines()[0]
        categories[category]+=1
        examples.setdefault(category,dict(entry=item['entry'],diagnostic=str(failure)))
report=dict(snapshotUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),cook=status,
            computeJobsWithRetainedSource=count,backendSuccesses=successful,backendFailures=failed,
            incompleteJobs=count-successful-failed,
            note='Compute backend jobs only, including the standalone compiler probe run during this interval; stage and preprocessing failures are reported in the full Unreal cook log. Counts are a snapshot, not a scene success result.',
            categories=[dict(error=k,count=v,example=examples[k]) for k,v in categories.most_common()])
(out/'diagnostic-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
