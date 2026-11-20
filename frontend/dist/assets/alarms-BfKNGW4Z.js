import{h as r}from"./http-BldnokKo.js";const t={list:a=>r.get("/api/alarms",{params:a}),ack:a=>r.put(`/api/alarms/${a}/ack`),recover:a=>r.put(`/api/alarms/${a}/recover`)};export{t as a};
