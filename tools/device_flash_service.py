"""Private GUI worker boundary; adapters own all model-specific operations."""
import argparse
from dataclasses import asdict
import json
from flash_models import adapters


def worker():
    parser=argparse.ArgumentParser(description='Private GUI device worker')
    parser.add_argument('--gui-worker',choices=('inspect','flash'),required=True)
    parser.add_argument('--model',required=True)
    parser.add_argument('--token',required=True)
    parser.add_argument('--action')
    parser.add_argument('--image')
    parser.add_argument('--sha256')
    args=parser.parse_args()
    def emit(kind,payload): print(json.dumps({'kind':kind,'payload':payload}),flush=True)
    try:
        adapter=adapters()[args.model]
        if args.gui_worker=='inspect': emit('device',asdict(adapter.inspect(args.token)))
        else:
            if not args.action or not args.image or not args.sha256: raise ValueError('Incomplete flash request')
            digest=adapter.flash(args.token,args.action,args.image,args.sha256,
                lambda done,total:emit('progress',[done,total]),lambda message:emit('status',message))
            emit('done',digest)
        return 0
    except Exception as error:
        emit('error',str(error));return 1


if __name__=='__main__': raise SystemExit(worker())
