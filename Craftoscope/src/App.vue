<template>
    <div id="app">
        <MineoramaMap />
    </div>
</template>

<script>
    import MineoramaMap from './components/MineoramaMap.vue';

    export default {
        name: 'app',
        components: {
            MineoramaMap,
        },
    };
</script>

<style>
    *, *:before, *:after {
        box-sizing: inherit;
    }

    html {
        box-sizing: border-box;
    }

    body {
        margin: 0;
    }

    html, body, #app {
        height: 100%;
    }
</style>
