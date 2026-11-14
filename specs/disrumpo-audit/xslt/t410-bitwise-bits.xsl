<?xml version="1.0" encoding="UTF-8"?>
<!--
  t410-bitwise-bits.xsl - fix plan T4.10 (D-173). Identity transform plus one deletion:

    TypeParams_Bitwise: the CViewContainer bitwise-group="bits" (Bits label + CSlider on Band.NodeShape3). Bitwise
    Mangler reads shape slots 0-2 only, and BitwiseOpController always hid this group (BitRotate / BitShuffle were
    removed), so it was dead markup bound to a slot nothing reads.

  Apply:  npx xslt3 -xsl:specs/disrumpo-audit/xslt/t410-bitwise-bits.xsl
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

  <xsl:template match="template[@name = 'TypeParams_Bitwise']/view[@bitwise-group = 'bits']">
    <xsl:if test="not(view[@control-tag = 'Band.NodeShape3'])">
      <xsl:message terminate="yes">bitwise-group="bits" no longer holds the Band.NodeShape3 slider</xsl:message>
    </xsl:if>
  </xsl:template>

</xsl:stylesheet>
