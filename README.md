# vUbercool
Very high performance web application daemon

Copyright 2014 by Jarle (jgaa) Aase. All rights reserved.
This project is licensed under the GNU version 3 license. Please
see the LICENSE file.

You can contact the developer at jarle@jgaa.com

# Background
A few years ago, a company issued an annual report about the
popularity of web (HTTP) servers. Each year, Microsoft IIS
achieved ridicules high scores. At that time I worked for a huge
(top 10) US software company as a senior C++ guy, and in a lunch
break we discussed this madness. Everyone agreed that IIS cannot
compete in the web server space. It's all about Apache and nginx.
Sure - there are lots of companies running IIS to host their
web pages, but they are getting fewer as Internet becomes a more
hostile place, and front facing servers require professional
attention.

So why could this be? I looked at the report, and noticed that
the popularity was based on number of distinct sites (host names) - not
IP numbers served, or number of distinct page hits. I suggested that
these absurd numbers may be deliberately manipulated by Microsoft, if they
hosted millions of idle sites in their data-centers, using free
cloud servers to simply answer on request from the web-spiders that
create such reports.

At that time there was about 1 billion distinct web sites, according to this
report. As a joke, I suggested that I could write a HTTP server
by myself and host another 1 billion web sites, and make my web server
the most popular on the Internet in a week!

It was actually a fun idea, and as anyone who have worked in a large
software company knows, progress there is  </i>s l o w . . .</i>

So I started to code my 1 billion site web server on my spare time,
finally making some progress. I wrote an embedded DNS server in 16
hours over 3 days. I spent the same 3 days, at work, getting a small,
insignificant bug-fix trough code review and submit process (where the
tests failed and failed and failed because of errors in the testing
infrastructure).

About a month later, I ran 1 billion distinct web sites on my laptop,
(ThinkPad w520 with 16 GB ram and Debian Linux). The
performance tests (querying from another machine) showed that the server
handled 170.000 page requests per second. That maxed out the bandwidth on
the 1GB network interface.

vUbercool can really serve 1 billion websites with distinct state (32
bits state), a few real websites with static content and a number of
sites using C++ generated content from different content generators.
To serve 1 billion host names (from the same domain name), it has
an embedded DNS server.

The server is written in C++11, using boost::asio for IO.

# Plans
I bought the domain name onebillionsites.com, but never found
an internet provider that could deliver the bandwidth I needed
to host 50% of the Internet on my laptop at home.

However, I still like the idea, so I plan to keep the code working,
and use my own [wfde](https://sourceforge.net/projects/wfde/)
library as much as possible. That allows me
to tune wfde for performance, and use vUbercool as a showcase
and benchmark tool - and may be the foundation of a real
web application server in C++ in the future (my old work-horse WarCMS
is long overdue for replacement).

Anyway, the code for the original project is here. Have fun.
