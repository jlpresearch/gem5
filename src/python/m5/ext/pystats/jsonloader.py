from .simstat import SimStat
from .statistic import Scalar, Distribution, Accumulator
from .group import Group, Vector
import json

def json_loader(path: str) -> SimStat:
    
    with open(path) as f:
        simstat_obj = json.load(f, object_hook=__json_to_simstat)

    return simstat_obj

def __json_to_simstat(d: dict) -> SimStat:
    print(d)
    if 'type' in d:
        if d['type'] == 'Scalar':
            return Scalar(
                          value=d.get('value'),
                          unit=d.get('unit'),
                          description=d.get('description'),
                          datatype=d.get('datatype')
                         )
        elif d['type'] == 'Distribution':
            return Distribution(
                                value=d.get('value'),
                                min=d.get('min'),
                                max=d.get('max'),
                                num_bins=d.get('numBins'),
                                bin_size=d.get('binSize'),
                                sum=d.get('sum'),
                                sum_squared=d.get('sumSquared'),
                                underflow=d.get('underflow'),
                                overflow=d.get('overflow'),
                                logs=d.get('logs'),
                                unit=d.get('unit'),
                                description=d.get('description'),
                                datatype=d.get('datatype')
                               )
        elif d['type'] == 'Accumulator':
            return Accumulator(
                                value=d.get('value'),
                                count=d.get('count'),
                                min=d.get('min'),
                                max=d.get('max'),
                                sumSquared=d.get('sum_squared'),
                                unit=d.get('unit'),
                                description=d.get('description'),
                                datatype=d.get('datatype')
                               )
        elif d['type'] == 'Group':
            return Group(**d)
        elif d['type'] == 'Vector':
            return Group(**d)
    else:
        return SimStat(**d)