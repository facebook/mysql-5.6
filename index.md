---
layout: home
title: Home
id: home
---

{% include content/gridblocks.html data_source=site.data.features align="center" %}


Left aligned:

{% include content/gridblocks.html data_source=site.data.features align="left" %}

Right aligned:

{% include content/gridblocks.html data_source=site.data.features align="right" %}

Images on the side:

{% include content/gridblocks.html data_source=site.data.features imagealign="side" %}

Four column layout:

{% include content/gridblocks.html data_source=site.data.features layout="fourColumn" align="center" %}
