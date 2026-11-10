<?xml version="1.0" encoding="UTF-8"?>
<!--
  header-bias-group.xsl - band header Bias knob shown only for types that use bias (user ruling 2026-11-10, after
  follow-up 87). Identity transform plus one regrouping:

    BandExpandedStrip, header row (CViewContainer 680 x 40): the "Bias" CTextLabel (330, 2, 40 x 12) and the
    Band.NodeBias ArcKnob (335, 14, 30 x 24) move into one transparent CViewContainer at 330, 2, 40 x 36 (label at
    0, 0, knob at 5, 12: the same screen positions), so the controller can hide both with the container
    (ContainerVisibilityController, by the knob's tag) when the selected node's type has no Bias slot.

  Apply:  npx xslt3 -xsl:specs/disrumpo-audit/xslt/header-bias-group.xsl
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

  <xsl:variable name="header"
                select="//template[@name = 'BandExpandedStrip']/view[view[@control-tag = 'Band.NodeBias']]"/>

  <xsl:template match="/">
    <xsl:if test="count($header) ne 1 or count($header/view[@class = 'CTextLabel' and @title = 'Bias']) ne 1">
      <xsl:message terminate="yes">expected one BandExpandedStrip header with one Bias label and knob</xsl:message>
    </xsl:if>
    <xsl:apply-templates/>
  </xsl:template>

  <!-- The label becomes the group: label and knob inside one container at the label's origin -->
  <xsl:template match="template[@name = 'BandExpandedStrip']/view/view[@class = 'CTextLabel' and @title = 'Bias'
                       and following-sibling::view[1][@control-tag = 'Band.NodeBias']]">
    <xsl:if test="@origin ne '330, 2' or following-sibling::view[1]/@origin ne '335, 14'">
      <xsl:message terminate="yes">Bias label / knob not at 330, 2 / 335, 14</xsl:message>
    </xsl:if>
    <view class="CViewContainer" origin="330, 2" size="40, 36" transparent="true">
      <xsl:copy>
        <xsl:apply-templates select="@*" mode="origin"><xsl:with-param name="origin" select="'0, 0'"/></xsl:apply-templates>
      </xsl:copy>
      <xsl:for-each select="following-sibling::view[1]">
        <xsl:copy>
          <xsl:apply-templates select="@*" mode="origin"><xsl:with-param name="origin" select="'5, 12'"/></xsl:apply-templates>
        </xsl:copy>
      </xsl:for-each>
    </view>
  </xsl:template>

  <!-- Attributes in their own order, the origin replaced -->
  <xsl:template match="@*" mode="origin">
    <xsl:copy/>
  </xsl:template>
  <xsl:template match="@origin" mode="origin">
    <xsl:param name="origin"/>
    <xsl:attribute name="origin" select="$origin"/>
  </xsl:template>

  <!-- The knob itself is emitted inside the group -->
  <xsl:template match="template[@name = 'BandExpandedStrip']/view/view[@control-tag = 'Band.NodeBias']"/>

</xsl:stylesheet>
