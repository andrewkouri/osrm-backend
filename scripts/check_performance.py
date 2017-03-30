#!/usr/bin/python3

# The script runs random queries for CH and MLD algorithms and
# and saves results (query time,  route weight, route distance) to a csv file
#
# References:
#  1. 2017 - Routing Engines für OpenStreetMap https://www.youtube.com/watch?v=rpKU-8J4HAo

from optparse import OptionParser
from subprocess import Popen, check_output, PIPE
import numpy as np
import os
import re
import sys
import shutil
import requests
import json

to_bbox = lambda x: {'west': float(x[0]), 'south': float(x[1]), 'east': float(x[2]), 'north': float(x[3])}
def get_bounding_boxes(osm_file):
    out = check_output(['osmium', 'fileinfo', osm_file], universal_newlines=True)
    coord = '[+-]?(?:\d+(?:\.\d*)?|\.\d+)'
    return list(map(to_bbox, re.findall('\((' + coord + '),(' + coord + '),(' + coord + '),(' + coord + ')', out)))

osm_basename = lambda x: x[:x.find('.osm')]
def start_ch_server(options, osm):
    osm_file = osm.replace('.osm', '_ch.osm')
    osrm_file = osm_basename(osm_file) + '.osrm'
    prefix, profile = options.prefix, options.profile
    if not os.path.exists(osrm_file):
        shutil.copyfile(osm, osm_file)
        Popen([os.path.join(prefix, 'osrm-extract'), '-p', profile, osm_file]).wait()
        Popen([os.path.join(prefix, 'osrm-contract'), osrm_file]).wait()
    return Popen([os.path.join(prefix, 'osrm-routed'), '-a', 'CH', osrm_file], stdout=PIPE, universal_newlines=True)

def start_mld_server(options, osm):
    osm_file = osm.replace('.osm', '_mld.osm')
    osrm_file = osm_basename(osm_file) + '.osrm'
    prefix, profile = options.prefix, options.profile
    if not os.path.exists(osrm_file):
        shutil.copyfile(osm, osm_file)
        Popen([os.path.join(prefix, 'osrm-extract'), '-p', profile, osm_file]).wait()
        Popen([os.path.join(prefix, 'osrm-partition'), osrm_file]).wait()
        Popen([os.path.join(prefix, 'osrm-customize'), osrm_file]).wait()
    return Popen([os.path.join(prefix, 'osrm-routed'), '-a', 'MLD', osrm_file], stdout=PIPE, universal_newlines=True)

def stop_server(process):
    process.terminate()
    process.wait()

def run_queries(start_server, queries):
    process = start_server()
    for stdout_line in iter(process.stdout.readline, ""):
        if stdout_line.find('waiting for requests') >= 0:
            break

    results = []
    for query in queries:
        response = requests.get(query)
        route = json.loads(response.text)['routes'][0]
        results += ((response.elapsed.total_seconds(), route['weight'], route['distance']),)
    stop_server(process)
    return results

def main():
    ## parse arguments
    parser = OptionParser('usage: %prog [options] osm')
    parser.add_option('-p', '--profile', dest='profile', help='use profile FILENAME', default='../profiles/car.lua')
    parser.add_option('-x', '--prefix', dest='prefix', help='OSRM binaries PREFIX', default=os.getcwd())
    parser.add_option('-b', '--bounding-box', dest='bbox', help='comma-separated list bounding box for queries west,south,east,north')
    parser.add_option('-n', '--num-queries', dest='num_queries', help='number of queries', type='int', default=1000)
    parser.add_option('-q', '--quiet', action='store_false', dest='verbose', default=True, help="don't print status messages to stdout")
    parser.add_option('-o', '--output', dest='output', help='output FILE')
    (options, args) = parser.parse_args()
    if len(args) != 1:
        parser.error('incorrect number of arguments')
    osm_file = args[0]
    if not osm_file.endswith('.osm') and not osm_file.endswith('.osm.pbf'):
        parser.error('incorrect file format')
    output_file = options.output if options.output is not None else osm_basename(osm_file) + '.csv'

    ## find bounding box
    if options.bbox is not None:
        bbox = to_bbox(options.bbox.split(','))
    elif shutil.which('osmium'):
        bbox = get_bounding_boxes(osm_file)
        if len(bbox) == 0:
            sys.exit(osm_file + ' has no bounding boxes')
        bbox = bbox[0] # TODO use only the first bounding box
    else:
        sys.exit('no bounding box is provided')

    ## generate queries
    lon = bbox['west'] + (bbox['east']-bbox['west']) * np.random.random((options.num_queries, 2))
    lat = bbox['south'] + (bbox['north']-bbox['south']) * np.random.random((options.num_queries, 2))
    coords = np.hstack((lon, lat))
    queries = ['http://localhost:5000/route/v1/driving/{0},{2};{1},{3}'.format(*x.tolist()) for x in coords]

    ## run queries and save results
    ch_results = run_queries(lambda : start_ch_server(options, osm_file), queries)
    mld_results = run_queries(lambda : start_mld_server(options, osm_file), queries)
    with open(output_file, 'wt') as csvfile:
        csvfile.write('source_lon,source_lat,target_lon,target_lat,ch_query_time,ch_weight,ch_distance,mld_query_time,mld_weight,mld_distance\n')
        for i in range(options.num_queries):
            csvfile.write('{0},{2},{1},{3},{4},{5},{6},{7},{8},{9}\n'.format(*(coords[i].tolist()), *ch_results[i], *mld_results[i]))

if __name__ == '__main__':
    main()
