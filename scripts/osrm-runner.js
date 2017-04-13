#!/usr/bin/env node

const http = require('http');
const process = require('process');
const cla = require('command-line-args');
const clu = require('command-line-usage');
const jq = require('node-jq');
const turf = require('turf');
const util = require('util');


const run_query = (query_options, filters, callback) => {
    let tic = () => 0.;
    let req = http.request(query_options, function (res) {
        let body = '', ttfb = tic();
        if (res.statusCode != 200)
            return callback(query_options.path, res.statusCode, ttfb);

        res.setEncoding('utf8');
        res.on('data', function (chunk) {
            body += chunk;
        });
        res.on('end', function () {
            const elapsed = tic();
            let json = JSON.parse(body);
            // remove 'geometry', 'waypoints', 'hint' that can contain bash special symbols
            json = JSON.stringify(json, (k,v) => (k === 'geometry') || (k === 'waypoints') || (k === 'hint') ? undefined : v);

            Promise.all(filters
                        .map(filter => jq.run(filter, json, {input: 'string', output: 'string'})
                             .then(x => x)
                             .catch(x => 'invalid filter ' + filter + ' ' + x)))
                .then(values => callback(query_options.path, res.statusCode, ttfb, elapsed, values));
        });
    }).on('socket', function (res) {
        tic = ((toc) => { return () => process.hrtime(toc)[1] / 1000000; })(process.hrtime());
    }).on('error', function (res) {
        callback(query_options.path, res.code);
    }).end();
}

function generate_points(polygon, number) {
    let query_points = [];
    while (query_points.length < number) {
    var chunk = turf
        .random('points', number, { bbox: turf.bbox(polygon)})
        .features
        .map(x => x.geometry.coordinates)
        .filter(pt => turf.inside(pt, polygon));
        query_points = query_points.concat(chunk);
    }
    return query_points.slice(0, number);
}

function generate_queries(options, query_points, coordinates_number) {
    let queries = [];
    for (let chunk = 0; chunk < query_points.length; chunk += coordinates_number)
    {
        let points = query_points.slice(chunk, chunk + coordinates_number);
        let query = options.path.replace(/{}/g, x =>  points.pop().join(','));
        queries.push({
            hostname: options.server.hostname,
            port: options.server.port,
            path: query
        });
    }
    return queries;
}

// Command line arguments
function ServerDetails(x) {
    if (!(this instanceof ServerDetails)) return new ServerDetails(x);
    const v = x.split(':');
    this.hostname = (v[0].length > 0) ? v[0] : '';
    this.port = (v.length > 1) ? Number(v[1]) : 80;
}
function BoundingBox(x) {
    if (!(this instanceof BoundingBox)) return new BoundingBox(x);
    const v = x.match(/[+-]?\d+(?:\.\d*)?|\.\d+/g);
    this.poly = turf.bboxPolygon(v.slice(0,4).map(x => Number(x)));
}
const optionsList = [
    {name: 'help', alias: 'h', type: Boolean, description: 'Display this usage guide.', defaultValue: false},
    {name: 'server', alias: 's', type: ServerDetails, defaultValue: ServerDetails('localhost:5000'),
     description: 'OSRM routing server', typeLabel: '[underline]{hostname[:port]}'},
    {name: 'path', alias: 'p', type: String, defaultValue: '/route/v1/driving/{};{}',
     description: 'OSRM query path with {} coordinate placeholders, default /route/v1/driving/{};{}', typeLabel: '[underline]{path}'},
    {name: 'filter', alias: 'f', type: String, defaultValue: ['.routes[].legs[].weight'], multiple: true,
     description: 'jq filters, default ".routes[].legs[].weight"', typeLabel: '[underline]{filter}'},
    {name: 'bounding-box', alias: 'b', type: BoundingBox, defaultValue: BoundingBox('5.86442,47.2654,15.0508,55.1478'), multiple: true,
     description: 'queries bounding box, default "5.86442,47.2654,15.0508,55.1478"', typeLabel: '[underline]{west,south,east,north}'},
    {name: 'number', alias: 'n', type: Number, defaultValue: 10,
     description: 'number of query points, default 10', typeLabel: '[underline]{num}'}
]
const options = cla(optionsList);
if (options.help) {
    const usage = clu([
        { header: 'Run OSRM queries and collect results'/*, content: 'Generates something [italic]{very} important.'*/ },
        { header: 'Options', optionList: optionsList }
    ]);
    console.log(usage);
    process.exit(0);
}

// TODO: add union of areas by OSM ids similar to
// curl -sg 'http://overpass-api.de/api/interpreter?data=[out:json];area(3600062428)->.area;rel(pivot.area);out%20geom;'
const polygon = options['bounding-box'].map(x => x.poly).reduce((x,y) => turf.union(x, y));
const coordinates_number = (options.path.match(/{}/g) || []).length;
const query_points = generate_points(polygon, coordinates_number * options.number);
const queries = generate_queries(options, query_points, coordinates_number);

queries.map(query => {
    run_query(query, options.filter, (query, code, ttfb, total, results) => {
        let str = `"${query}",${code}`;
        if (ttfb !== undefined) str += `,${ttfb}`;
        if (total !== undefined) str += `,${total}`;
        if (typeof results === 'object' && results.length > 0)
            str += ',' + results.map(x => isNaN(x) ? '"' + x.replace(/\n/g, ';').replace(/"/g, "'") + '"' : Number(x)).join(',');
        console.log(str);
    });
});
