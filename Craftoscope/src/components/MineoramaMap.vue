<template>
    <div class="map" ref="map"></div>
</template>

<script>
    import L from "leaflet";

    L.TileLayer.Mineorama = L.TileLayer.extend({
        _tileIndex: null,
        options: {
            dimension: 0,
            layer: 255,
            minNativeZoom: 0,
            maxNativeZoom: 0,
        },
        initialize: function (tileIndex, options) {
            this._tileIndex = tileIndex;
            L.TileLayer.prototype.initialize.call(this, '/tiles/tile_{dimension}_{_displayLayer}_{x}_{y}.png', options)
            L.setOptions(this, options);
        },
        getTileUrl(coords) {
            if (this._tileIndex[this.options.dimension] === undefined) {
                return '';
            }

            if (this._tileIndex[this.options.dimension][coords.x] === undefined) {
                return '';
            }

            if (this._tileIndex[this.options.dimension][coords.x][coords.y] === undefined) {
                return '';
            }

            const limit = this._tileIndex[this.options.dimension][coords.x][coords.y];

            // Re-purpose z as the layer index.
            let displayLayer = this.options.layer;
            if (displayLayer > limit) {
                displayLayer = limit;
            }

            this.options._displayLayer = displayLayer;
            return L.TileLayer.prototype.getTileUrl.call(this, coords);
        },
    });
    
    L.tileLayer.mineorama = function (options) {
        return fetch('/tiles/index.json')
            .then(response => response.json())
            .then(tileIndex => new L.TileLayer.Mineorama(tileIndex, options));
    };

    export default {
        name: 'MineoramaMap',
        mounted() {
            this.$options.map = L.map(this.$refs.map, {
                attributionControl: false,
                zoomSnap: 0.5,
                zoomDelta: 1,
                crs: L.CRS.Simple,
                center: [-234, -137],
                zoom: 2,
            });

            L.tileLayer.mineorama({
                minZoom: -5,
                maxZoom: 7,
            }).then((layer) => {
                this.$options.layer = layer.addTo(this.$options.map);
            });
        },
        map: null,
    };
</script>

<style>
    @import "../../node_modules/leaflet/dist/leaflet.css";

    .leaflet-container {
        background: #000;
    }

    img.leaflet-tile {
        image-rendering: pixelated;
    }
</style>

<style scoped>
    .map {
        width: 100%;
        height: 100%;
    }
</style>