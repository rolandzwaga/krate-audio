<?xml version="1.0" encoding="UTF-8"?>
<!--
  t303-morph-smoothing.xsl - fix plan T3.03 (D-152). Identity transform plus one move:

    BandExpandedStrip, Morph column (CViewContainer at 0, 45, 200 x 230): the Smooth ArcKnob (Band.MorphSmoothing,
    30 x 25) origin 75, 200 -> 75, 204. The Nodes segment button (Band.ActiveNodes) spans y 182..202, so the knob's
    top 2 px sat under it; at y 204 the knob spans 204..229, inside the 230-tall column.

  Every other overlap uidesc-inventory reports for this file was ruled intentional by the audit (D-150, D-151, D-153
  refuted) and is accepted by plugins/disrumpo/tests/uidesc_overlaps_allowed.json, not moved.

  Apply:  npx xslt3 -xsl:specs/disrumpo-audit/xslt/t303-morph-smoothing.xsl
                    -s:plugins/disrumpo/resources/editor.uidesc -o:OUT
  then restore the file's own formatting on OUT exactly as for t222-sweep-controls.xsl (UTF-8 BOM, CRLF line ends,
  no final line end, each comment on its own line, the empty <bitmaps> element written open / close).
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="saxon">

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name = 'BandExpandedStrip']//view[@control-tag = 'Band.MorphSmoothing']/@origin">
    <xsl:if test=". ne '75, 200'">
      <xsl:message terminate="yes">Band.MorphSmoothing origin is <xsl:value-of select="."/>, expected 75, 200</xsl:message>
    </xsl:if>
    <xsl:attribute name="origin">75, 204</xsl:attribute>
  </xsl:template>

</xsl:stylesheet>
