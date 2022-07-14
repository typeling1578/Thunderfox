//UA version
{
    const ua_versions = [
        {
            "version": 102,
            "start": "2022-06-28T14:00:00+0000"
        },
        {
            "version": 103,
            "start": "2022-07-26T14:00:00+0000"
        },
        {
            "version": 104,
            "start": "2022-08-23T14:00:00+0000"
        },
        {
            "version": 105,
            "start": "2022-09-20T14:00:00+0000"
        },
        {
            "version": 106,
            "start": "2022-10-18T14:00:00+0000"
        },
        {
            "version": 107,
            "start": "2022-11-15T14:00:00+0000"
        },
        {
            "version": 108,
            "start": "2022-12-13T14:00:00+0000"
        }
    ]

    let now = (new Date()).getTime();
    let match = ua_versions.filter(ua_version => {
        if (now >= (new Date(ua_version.start)).getTime()) {
            return true;
        }
        return false;
    })
    if (match && match.length > 0) {
        lockPref("network.http.useragent.version", match[match.length - 1].version);
    } else {
        lockPref("network.http.useragent.version", 102);
    }
}
