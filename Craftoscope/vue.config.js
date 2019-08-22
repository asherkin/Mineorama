const express = require('express');

module.exports = {
    devServer: {
        before(app) {
            app.use('/tiles', express.static('C:\\Users\\asherkin\\Downloads\\mcpe_viz-master\\work\\output', {
                fallthrough: false,
            }));
        },
    },
};